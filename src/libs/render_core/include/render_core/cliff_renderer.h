// Cliff pass (cliff3d assets): surface-nets cliffs from the vertex-node grid
// encoded in CliffLandscape layer tiles. Port of the TileShapePlayground cliff
// pass (AtlasRenderer) into the shared world render.
//
// Differences from the playground original:
// - the camera is applied in the vertex shader (view_size/camera_offset/
//   camera_zoom uniforms), so the cached vertex stream survives pan/zoom;
// - vertices carry the raw ground fieldY as z, normalized in the VS via the
//   z_range uniform (same convention as the raised pass — depths are
//   comparable across the two passes in the shared depth buffer);
// - the backend is picked at runtime via sg_query_backend().
//
// The pass needs a depth attachment: with SG_PIXELFORMAT_NONE at init it is
// disabled (no painter fallback — surface nets have no sortable primitives).
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "render_core/sokol_config.h"

#include <highground_core/cliff_field.h>
#include <highground_core/surface_nets.h>

#include "render_core/landscape_renderer.h" // LandscapeTile, camera, iso

namespace render_core {

// Shading palette/light of a cliff3d asset (uniforms only — edits apply
// instantly, no mesh rebuild). Mirrors BaseData::Cliff3dShadingData and the
// TileShapePlayground ShadingParams.
struct CliffShading {
    float lightAzimuth = 2.23f;   // radians
    float lightElevation = 0.85f; // radians
    std::array<float, 3> darkColor{0.38f, 0.38f, 0.42f};
    std::array<float, 3> goldColor{0.75f, 0.62f, 0.5f};
    std::array<float, 3> grassA{0.4f, 0.62f, 0.35f};
    std::array<float, 3> grassB{0.6f, 0.65f, 0.4f};
    float veinThreshold = 0.8f;
    float ambient = 0.35f;
    float diffuse = 0.75f;
    float backLight = 0.1f;
    float specStrength = 0.5f;
    float specPower = 24.0f;
    float gamma = 0.85f;
};

// Full parameter set of a cliff3d asset (mirror of BaseData/game_data
// Cliff3dAssetData): scalar field + shading + the height scale.
struct CliffParams {
    cliff::FieldParams field;
    CliffShading shading;
    float heightScale = 96.0f; // field px per 1.0 plateau height
};

class CliffRenderer {
public:
    // depthFormat must match the pass the renderer draws into; the pass is
    // disabled when depthFormat == SG_PIXELFORMAT_NONE.
    void init(sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL);
    void shutdown();

    // Register/update the generator+shading params of a cliff3d assetUuid.
    // Cheap: param edits only invalidate the affected cache (field params —
    // debounced mesh rebuild, heightScale — cheap re-projection, shading —
    // uniforms only).
    void ensureCliffAsset(const std::string& assetUuid, const CliffParams& params);

    // Draw the cliff tiles (CliffLandscape layer content). Tiles whose
    // assetUuid was never ensured are skipped. `nowSec` drives the edit
    // debounce (heavy field rebuilds run at most 0.3 s after the last edit).
    void render(
        const std::vector<LandscapeTile>& tiles,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight,
        double nowSec);

private:
    // Vertex stream of a cache entry: field-space position (pre-camera;
    // pos.z = raw ground fieldY), field normal, groove carve attribute and the
    // world position (map cells, height) for per-pixel shading.
    struct CliffVertex {
        float pos[3];
        float normal[3];
        float groove;
        float world[3];
    };

    // VS uniforms: camera + depth normalization (28 bytes; the GLSL backend
    // requires the declared uniforms to sum exactly to the block size).
    struct CliffVsParams {
        float view_size[2];
        float camera_offset[2];
        float z_range[2];
        float camera_zoom;
    };

    // FS uniforms (palette/light, 8xvec4 = 128 bytes, slot 1).
    struct CliffFsParams {
        float lightDir[4];
        float viewDir[4];
        float darkColor[4];
        float goldColor[4];
        float grassA[4];
        float grassB[4];
        float params0[4]; // vein threshold, ambient, diffuse, spec strength
        float params1[4]; // spec power, gamma, wrap backlight, unused
    };

    // Cached derivative of one asset's cliff tiles: the extracted surface-nets
    // meshes (one per connected node region, so distant islands never inflate
    // a shared field bbox) plus the projected vertex stream, rebuilt only when
    // the tile set or the field params change (debounced) — the full rebuild
    // costs seconds.
    struct CliffCache {
        std::uint64_t contentHash = 0;
        float heightScale = 0.0f;
        bool contentValid = false;
        double lastEditSec = 0.0;
        bool meshDirty = false;   // field + extraction needed (heavy)
        bool streamDirty = false; // re-projection only (cheap, heightScale edits)
        bool gpuDirty = false;
        cliff::FieldParams fieldParams{};
        struct Region {
            cliff::Mesh mesh;
            glm::ivec2 origin{0, 0}; // node coords of the region field's (0,0)
        };
        std::vector<Region> regions;
        std::vector<CliffVertex> stream;
        sg_buffer vbuf{SG_INVALID_ID};
        std::size_t vbufSize = 0;
        bool watertight = true;
    };

    void ensurePipeline();
    void destroyPipeline();
    void rebuildCliffCache(
        CliffCache& cache,
        const std::vector<const LandscapeTile*>& group,
        const topology_core::DiamondIsometry& iso);

    sg_pipeline pip{SG_INVALID_ID};
    sg_shader shd{SG_INVALID_ID};
    sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;

    std::unordered_map<std::string, CliffParams> assets;
    std::unordered_map<std::string, CliffCache> caches;

    // Prototype silhouette: while a group's cache is pending (edit waiting
    // for the debounce/rebuild), the tiles are drawn as flat palette-shaded
    // diamonds through the same pipeline instead of the stale/missing mesh.
    // One scratch buffer, uploaded at most once per frame.
    struct PreviewRange {
        int base = 0;
        int count = 0;
        const CliffParams* params = nullptr;
    };
    std::vector<CliffVertex> m_previewVerts;
    std::vector<PreviewRange> m_previewRanges;
    sg_buffer m_previewVbuf{SG_INVALID_ID};
    std::size_t m_previewVbufSize = 0;
};

} // namespace render_core
