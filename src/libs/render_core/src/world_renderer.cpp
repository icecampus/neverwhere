#include "render_core/world_renderer.h"

#include <algorithm>
#include <unordered_set>

#include "atlas_tile_types.h"

namespace render_core {

namespace {

// Contact AO field resolution (HighgroundWithEffects): 8 texels per cell —
// the falloff is a fraction of a cell wide, and at 4 the bilinear ramp spans
// under two texels and shows the chamfer transform's diagonal staircase —
// plus a 4-cell margin (the AO reach) around the map bbox.
constexpr int kAoTexelsPerCell = 8;
constexpr int kAoMarginCells = 4;

} // namespace

void WorldRenderer::init(sg_pixel_format depthFormat) {
    landscapeRenderer.init(depthFormat);
    cliffRenderer.init(depthFormat);
    cyclopeanRenderer.init(depthFormat);
    spriteRenderer.init(depthFormat);
    overlayRenderer.init(depthFormat);

    // Contact AO: the sampler + the 1x1 "nothing nearby" placeholder, so the
    // ground binding is complete even before the first prepare().
    sg_sampler_desc smp_desc = {};
    smp_desc.min_filter = SG_FILTER_LINEAR;
    smp_desc.mag_filter = SG_FILTER_LINEAR;
    smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    smp_desc.label = "world-ao-smp";
    m_aoSampler = sg_make_sampler(&smp_desc);

    const std::uint8_t far = 255;
    sg_image_desc img_desc = {};
    img_desc.width = 1;
    img_desc.height = 1;
    img_desc.pixel_format = SG_PIXELFORMAT_R8;
    img_desc.data.mip_levels[0].ptr = &far;
    img_desc.data.mip_levels[0].size = sizeof(far);
    img_desc.label = "world-ao-placeholder";
    m_aoImage = sg_make_image(&img_desc);
    sg_view_desc view_desc = {};
    view_desc.texture.image = m_aoImage;
    m_aoView = sg_make_view(&view_desc);

    m_stitchParams = buildStitchParams(stitch, m_aoField);
}

void WorldRenderer::shutdown() {
    overlayRenderer.shutdown();
    spriteRenderer.shutdown();
    cyclopeanRenderer.shutdown();
    cliffRenderer.shutdown();
    landscapeRenderer.shutdown();

    if (m_aoView.id != SG_INVALID_ID) {
        sg_destroy_view(m_aoView);
        m_aoView = {};
    }
    if (m_aoImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_aoImage);
        m_aoImage = {};
    }
    if (m_aoSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_aoSampler);
        m_aoSampler = {};
    }
}

void WorldRenderer::ensureLandscapeAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows) {
    landscapeRenderer.ensureAtlas(assetUuid, atlasPath, cols, rows);
}

void WorldRenderer::ensureRaisedAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows, const RaisedParams& params, const std::filesystem::path& topTexturePath) {
    landscapeRenderer.ensureRaisedAtlas(assetUuid, atlasPath, cols, rows, params, topTexturePath);
}

void WorldRenderer::ensureCliffAsset(const std::string& assetUuid, const CliffParams& params) {
    cliffRenderer.ensureCliffAsset(assetUuid, params);
}

void WorldRenderer::ensureCyclopeanAsset(const std::string& assetUuid, const CyclopeanParams& params) {
    cyclopeanRenderer.ensureCyclopeanAsset(assetUuid, params);
}

void WorldRenderer::ensureStoneAsset(const std::string& assetUuid, const CliffParams& params) {
    // Stone3d shares the cliff renderer (stone field + stone shading extras
    // ride inside CliffParams).
    cliffRenderer.ensureStoneAsset(assetUuid, params);
}

void WorldRenderer::ensureSpriteImage(const std::string& assetUuid, const std::filesystem::path& imagePath, float widthCells, const glm::vec2& pivot) {
    spriteRenderer.ensureImage(assetUuid, imagePath, widthCells, pivot);
}

void WorldRenderer::prepare(const WorldFrame& frame, double /*nowSec*/) {
    // Union node footprint of the highground layers. Only the layers that
    // actually produce height cast contact AO, and only tiles of a registered
    // asset (a tile without geometry casts nothing). Tiles carry the same
    // vertex-node encoding the renderers decode (atlas_tile_types.h).
    std::unordered_set<std::uint64_t> onNodes;
    int maxX = -1;
    int maxY = -1;
    const auto addTiles = [&](const std::vector<LandscapeTile>& tiles, const auto& assetKnown) {
        for (const LandscapeTile& t : tiles) {
            if (!assetKnown(t.assetUuid)) continue;
            const auto mask = landscape_core::tileTypeToNodeMask(tileTypeFromAtlasIndex(t.tileIndex));
            const auto corners = topology_core::DiamondIsometry::cellCornerNodes(t.cell);
            for (int i = 0; i < 4; ++i) {
                if (!mask[i]) continue;
                onNodes.insert(nodeKey(corners[i]));
                maxX = std::max(maxX, corners[i].x);
                maxY = std::max(maxY, corners[i].y);
            }
        }
    };
    const auto cliffKnown = [this](const std::string& uuid) {
        return cliffRenderer.findAsset(uuid) != nullptr;
    };
    addTiles(frame.cliffTiles, cliffKnown);
    addTiles(frame.stoneTiles, cliffKnown);
    addTiles(frame.cyclopeanTiles, [this](const std::string& uuid) {
        return cyclopeanRenderer.hasAsset(uuid);
    });

    // Content key: the sorted on-node set — the same content hashes
    // identically every frame, an edit rehashes and rebuilds the field.
    std::vector<std::uint64_t> sorted(onNodes.begin(), onNodes.end());
    std::sort(sorted.begin(), sorted.end());
    std::uint64_t key = 1469598103934665603ULL;
    for (const std::uint64_t k : sorted) {
        key ^= k;
        key *= 1099511628211ULL;
    }

    if (key != m_aoKey) {
        m_aoKey = key;

        m_aoField = ContactAoField{};
        if (!onNodes.empty()) {
            AoFootprint fp;
            fp.nodesX = maxX + 1;
            fp.nodesY = maxY + 1;
            fp.nodeOn = [&onNodes](int x, int y) {
                return onNodes.find(nodeKey({x, y})) != onNodes.end();
            };
            buildContactAoField(&fp, 1, kAoTexelsPerCell, kAoMarginCells, m_aoField);
        }

        // (Re)upload the field texture; an empty field falls back to the 1x1
        // "nothing nearby" placeholder (any uv clamps to "far").
        if (m_aoView.id != SG_INVALID_ID) {
            sg_destroy_view(m_aoView);
            m_aoView = {};
        }
        if (m_aoImage.id != SG_INVALID_ID) {
            sg_destroy_image(m_aoImage);
            m_aoImage = {};
        }
        const std::uint8_t far = 255;
        sg_image_desc img_desc = {};
        img_desc.pixel_format = SG_PIXELFORMAT_R8;
        if (m_aoField.empty()) {
            img_desc.width = 1;
            img_desc.height = 1;
            img_desc.data.mip_levels[0].ptr = &far;
            img_desc.data.mip_levels[0].size = sizeof(far);
            img_desc.label = "world-ao-placeholder";
        } else {
            img_desc.width = m_aoField.width;
            img_desc.height = m_aoField.height;
            img_desc.data.mip_levels[0].ptr = m_aoField.texels.data();
            img_desc.data.mip_levels[0].size = m_aoField.texels.size();
            img_desc.label = "world-ao-field";
        }
        m_aoImage = sg_make_image(&img_desc);
        sg_view_desc view_desc = {};
        view_desc.texture.image = m_aoImage;
        m_aoView = sg_make_view(&view_desc);
    }

    // The sun/tone/AO block is cheap — settings edits apply every frame.
    m_stitchParams = buildStitchParams(stitch, m_aoField);
}

void WorldRenderer::appendCellDiamond(std::vector<LineSegment>& lines, const glm::vec2& center, const glm::vec2& halfSize, const glm::vec4& color) const {
    // Same 4 lines as the editor's DiamondGrid cell: Left -> Up -> Right -> Down -> Left.
    const glm::vec2 left  = center + glm::vec2(-halfSize.x, 0.0f);
    const glm::vec2 up    = center + glm::vec2(0.0f, -halfSize.y);
    const glm::vec2 right = center + glm::vec2(halfSize.x, 0.0f);
    const glm::vec2 down  = center + glm::vec2(0.0f, halfSize.y);

    lines.push_back({left, up, color});
    lines.push_back({up, right, color});
    lines.push_back({right, down, color});
    lines.push_back({down, left, color});
}

void WorldRenderer::render(
    const WorldFrame& frame,
    const topology_core::DiamondIsometry& iso,
    const topology_core::Camera2D& camera,
    int viewWidth,
    int viewHeight,
    double nowSec) {

    // Flat ground: stitched with the highground — shared sun/tone block plus
    // the contact-AO field texture from prepare().
    GroundStitchContext groundStitch;
    groundStitch.aoView = m_aoView;
    groundStitch.aoSampler = m_aoSampler;
    groundStitch.params = m_stitchParams;
    landscapeRenderer.render(frame.landscapeTiles, iso, camera, viewWidth, viewHeight, groundStitch);

    // Grid overlay: above the water and the flat ground, but UNDER the 3D
    // world (raised walls / cliffs / sprites overdraw it — no depth write
    // here, and the 3D passes both test and write depth).
    scratchLines.clear();

    if (frame.showGrid && viewWidth > 0 && viewHeight > 0 && camera.zoom > 0.0f) {
        // Editor parity (DiamondGrid): cell AABB of the viewport, capped —
        // a wildly zoomed-out view would otherwise produce millions of lines.
        const glm::vec2 viewSize((float)viewWidth, (float)viewHeight);
        const topology_core::CellRegion region = iso.visibleCellBounds(viewSize / camera.zoom, -camera.offset / camera.zoom);

        constexpr int kMaxCellsPerAxis = 256;
        const int cellsX = std::min(region.max.x - region.min.x + 1, kMaxCellsPerAxis);
        const int cellsY = std::min(region.max.y - region.min.y + 1, kMaxCellsPerAxis);

        const glm::vec2 cellSize = iso.dims.cellSize();
        const glm::vec2 halfSizeScreen = cellSize * 0.5f * camera.zoom;

        for (int i = 0; i < cellsX; ++i) {
            const int cx = region.min.x + i;
            for (int j = 0; j < cellsY; ++j) {
                const int cy = region.min.y + j;
                const glm::vec2 screenCenter = camera.worldToScreen(iso.mapToField({cx, cy}));
                appendCellDiamond(scratchLines, screenCenter, halfSizeScreen, frame.gridColor);
            }
        }
    }
    overlayRenderer.render(scratchLines, viewWidth, viewHeight);

    landscapeRenderer.renderRaised(frame.raisedTiles, iso, camera, viewWidth, viewHeight);

    // Cliff/stone passes: shared sun via the stitch core block + the seam
    // materials. The wall-foot AO shares the ground's AO switch. Both tile
    // sets go through ONE render call: they share the pipeline, the cache
    // machinery and its per-frame scratch buffers — two calls in one frame
    // would update the prototype-silhouette buffer twice, and sokol allows
    // only one sg_update_buffer per buffer per frame.
    CliffStitchContext cliffStitch;
    cliffStitch.params = m_stitchParams;
    cliffStitch.seam = seam;
    cliffStitch.aoWallFade = stitch.aoEnabled ? stitch.aoWallFade : 0.0f;
    scratchCliffStoneTiles.clear();
    scratchCliffStoneTiles.reserve(frame.cliffTiles.size() + frame.stoneTiles.size());
    scratchCliffStoneTiles.insert(scratchCliffStoneTiles.end(), frame.cliffTiles.begin(), frame.cliffTiles.end());
    scratchCliffStoneTiles.insert(scratchCliffStoneTiles.end(), frame.stoneTiles.begin(), frame.stoneTiles.end());
    cliffRenderer.render(scratchCliffStoneTiles, iso, camera, viewWidth, viewHeight, nowSec, cliffStitch);
    cyclopeanRenderer.render(frame.cyclopeanTiles, iso, camera, viewWidth, viewHeight, nowSec);
    spriteRenderer.render(frame.sprites, iso, camera, viewWidth, viewHeight);

    // The cell cursor is an editor UI element — always on top (own buffer:
    // sokol allows only one update per buffer per frame).
    scratchLines.clear();
    if (frame.cursorCell) {
        const glm::vec2 cellSize = iso.dims.cellSize();
        const glm::vec2 halfSizeScreen = cellSize * 0.5f * camera.zoom;
        const glm::vec2 screenCenter = camera.worldToScreen(iso.mapToField(*frame.cursorCell));
        appendCellDiamond(scratchLines, screenCenter, halfSizeScreen, frame.cursorColor);
    }
    overlayRenderer.renderTop(scratchLines, viewWidth, viewHeight);
}

} // namespace render_core
