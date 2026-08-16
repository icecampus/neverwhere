#pragma once

#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "render_core/landscape_renderer.h"
#include "render_core/cliff_renderer.h"
#include "render_core/cyclopean_renderer.h"
#include "render_core/fence_renderer.h"
#include "render_core/building_renderer.h"
#include "render_core/overlay_renderer.h"
#include "render_core/sprite_renderer.h"
#include "render_core/scene_stitch.h"

#include "topology_core/camera2d.h"
#include "topology_core/diamond_isometry.h"

namespace render_core {

// Plain render input for one frame — no Qt, no game data types, so any shell
// (game client, Qt editor, playground) can fill it from its own data source.
struct WorldFrame {
    std::vector<LandscapeTile> landscapeTiles;
    std::vector<LandscapeTile> raisedTiles; // RaisedLandscape layer (3D tiles: walls + lifted top)
    std::vector<LandscapeTile> cliffTiles;  // CliffLandscape layer (cliff3d: surface-nets cliffs)
    std::vector<LandscapeTile> cyclopeanTiles; // CyclopeanLandscape layer (cyclopean3d: landscape_mesh walls)
    std::vector<LandscapeTile> stoneTiles;  // StoneLandscape layer (stone3d: voronoi-carved surface-nets plateau, shares the cliff pass)
    std::vector<LandscapeTile> textureTiles; // TextureLandscape layer (texture2d: tiling world-UV textures, multi-texture blend)
    std::vector<LandscapeTile> techTiles;   // TechLandscape layer (tech3d: TechnicalGrass ridge/valley heightfield, shares the cliff pass)
    std::vector<LandscapeTile> maskTiles;   // MaskLandscape layer (mask3d: node-mask plate with a sloped skirt + PBR-lite material, shares the cliff pass)
    std::vector<FencePiece> fencePieces;    // FenceLandscape layer (fence3d: baked piece meshes, own pass)
    std::vector<SpriteInstance> sprites;
    std::vector<BuildingInstance> buildings; // building3d GLB instances (GameplayInteractive)

    // Fence tool transient state (editor only): the ghost preview piece list
    // (stroke plan / move preview; green = applicable, red = rejected) and
    // the selected fence (amber tint), -1 = no selection.
    std::vector<FencePiece> fenceGhost;
    bool fenceGhostValid = false;
    int selectedFenceId = -1;

    bool showGrid = true;
    glm::vec4 gridColor{0.5f, 0.5f, 0.5f, 1.0f}; // QML "grey"

    std::optional<glm::ivec2> cursorCell;
    glm::ivec2 cursorFootprint{1, 1}; // cell cursor size (building3d paints a 3x3, etc.)
    glm::vec4 cursorColor{1.0f, 0.0f, 0.0f, 1.0f}; // QML "red"
};

// Facade over the world renderers — the single world render shared by shells.
// Draw order: flat landscape tiles, grid overlay (above water/flat ground, and
// ON the ground plane of the 3D world: it writes depth, so the 3D overdraws it
// where it rises above the plane and stays behind it where it hangs below —
// base slabs, the underwater foot of a tech shoreline), raised landscape (cliff
// walls + lifted tops), cliff3d meshes, stone3d+tech3d meshes (same cliff
// pass), cyclopean3d meshes, fence3d piece meshes (+ the tool's ghost preview
// on top), texture2d cover, Tile2D sprites (vertical planes testing the
// published depth, LESS_EQUAL without depth write, painter order between
// themselves), and finally the cell cursor overlay on top.
//
// Scene stitching (HighgroundWithEffects port): the ground and the highground
// share one sun/tone block, the ground darkens around the highground
// footprint (contact AO, an R8 distance field rebuilt in prepare()) and the
// cliff pass carries the seam materials (rim contact AO, bounce/sky tints,
// plateau top tint/brightness/rotation, wall-foot AO).
class WorldRenderer {
public:
    // depthFormat must match the pass the renderer draws into:
    // SG_PIXELFORMAT_DEPTH_STENCIL for the sokol_app swapchain (game client, default),
    // SG_PIXELFORMAT_NONE for a Qt FBO without a wrapped depth attachment (editor).
    void init(sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL);
    void shutdown();

    void ensureLandscapeAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows);
    void ensureRaisedAtlas(const std::string& assetUuid, const std::filesystem::path& atlasPath, int cols, int rows, const RaisedParams& params, const std::filesystem::path& topTexturePath = {});
    void ensureCliffAsset(const std::string& assetUuid, const CliffParams& params);
    void ensureCyclopeanAsset(const std::string& assetUuid, const CyclopeanParams& params);
    void ensureStoneAsset(const std::string& assetUuid, const CliffParams& params);
    void ensureTechAsset(const std::string& assetUuid, const CliffParams& params);
    void ensureMaskAsset(const std::string& assetUuid, const CliffParams& params);
    void ensureFenceAsset(const std::string& assetUuid, const std::filesystem::path& meshDir, float metersToPoints);
    void ensureTextureAsset(const std::string& assetUuid, const std::filesystem::path& texturePath, float tilingRepeats);
    void ensureSpriteImage(const std::string& assetUuid, const std::filesystem::path& imagePath, float widthCells, const glm::vec2& pivot);
    void ensureBuildingAsset(const std::string& assetUuid, const BuildingParams& params);

    // Scene stitching knobs (playground defaults). C++ API only for now (no
    // QML); edits apply from the next prepare().
    SceneStitchSettings stitch;
    SeamParams seam;
    // Texture-2D layer blend knobs (SDFGeneratedLandscape defaults). C++ API
    // only for now (no QML); uniforms only, apply instantly.
    TextureBlendParams textureBlend;

    // Offscreen half of the frame: rebuilds the contact-AO field + its R8
    // texture when the highground content (cliff/stone/cyclopean tiles)
    // changed, and refreshes the shared stitch uniform block. MUST run before
    // the render pass is begun (texture re-creation is not allowed inside a
    // pass). `nowSec` is reserved (frame time of the shell).
    void prepare(const WorldFrame& frame, double nowSec);

    // `nowSec` drives the cliff cache debounce (frame time of the shell).
    void render(
        const WorldFrame& frame,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight,
        double nowSec);

private:
    LandscapeRenderer landscapeRenderer;
    CliffRenderer cliffRenderer;
    CyclopeanRenderer cyclopeanRenderer;
    FenceRenderer fenceRenderer;
    BuildingRenderer buildingRenderer;
    SpriteRenderer spriteRenderer;
    OverlayRenderer overlayRenderer;

    std::vector<LineSegment> scratchLines;
    // Merged cliff+stone tile list of the frame (one cliff-pass render call —
    // see render()).
    std::vector<LandscapeTile> scratchCliffStoneTiles;

    // Contact AO GPU side: the R8 distance field texture plus a 1x1 "nothing
    // nearby" placeholder bound while no highground exists (the ground shader
    // always declares the sampler, so the binding must stay complete).
    sg_image m_aoImage{SG_INVALID_ID};
    sg_view m_aoView{SG_INVALID_ID};
    sg_sampler m_aoSampler{SG_INVALID_ID};
    ContactAoField m_aoField;
    SceneStitchParams m_stitchParams{};
    std::uint64_t m_aoKey = 0;

    // depthCenter/depthHalf: normalized depth of the cell center and the delta
    // of the Up/Down corners (half a cell along the view ray).
    void appendCellDiamond(
        std::vector<LineSegment>& lines,
        const glm::vec2& screenCenter,
        const glm::vec2& halfSize,
        const glm::vec4& color,
        float depthCenter,
        float depthHalf) const;
};

} // namespace render_core
