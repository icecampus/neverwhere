// Cyclopean pass (cyclopean3d assets): landscape_mesh solid-mask plateau with
// Cyclopean-style walls, built from the vertex-node grid encoded in
// CyclopeanLandscape layer tiles. Port of the SDFGeneratedLandscape
// "Cyclopean 3D" wall-mesh pass (AtlasRenderer::rebuildWallMeshCache) into
// the shared world render.
//
// Differences from the playground original:
// - the camera is applied in the vertex shader (view_size/camera_offset/
//   camera_zoom uniforms), so the cached vertex stream survives pan/zoom;
// - vertices carry the raw ground fieldY as z, normalized in the VS via the
//   z_range uniform (same convention as the raised/cliff passes — depths are
//   comparable across the passes in the shared depth buffer);
// - colors are baked into vertices by the composer (no textures, no FS
//   uniforms).
//
// The pass needs a depth attachment: with SG_PIXELFORMAT_NONE at init it is
// disabled (same contract as the cliff pass).
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "render_core/sokol_config.h"

#include "render_core/landscape_renderer.h" // LandscapeTile, camera, iso

namespace render_core {

// Mesh-build params of a cyclopean3d asset (mirror of
// BaseData/game_data Cyclopean3dAssetData; wallStyle is always Cyclopean).
struct CyclopeanParams {
    float raisedHeight = 3.0f; // plateau top height in world units (topHeight)
    int rockSeed = 1337;
    float rockAmplitude = 0.28f;
    bool rockEnabled = true;
    float cornerBevel = 0.3f;
    int wallSubdivH = 16; // wallHorizontalSubdivisions (composer clamps to [1,16])
    int wallSubdivV = 16; // wallVerticalSubdivisions (composer clamps to [1,16])
};

class CyclopeanRenderer {
public:
    // depthFormat must match the pass the renderer draws into; the pass is
    // disabled when depthFormat == SG_PIXELFORMAT_NONE.
    void init(sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL);
    void shutdown();

    // Register/update the mesh params of a cyclopean3d assetUuid. Cheap: the
    // content hash is checked per frame and a change only schedules a
    // debounced rebuild.
    void ensureCyclopeanAsset(const std::string& assetUuid, const CyclopeanParams& params);

    // Draw the cyclopean tiles (CyclopeanLandscape layer content). Tiles
    // whose assetUuid was never ensured are skipped. `nowSec` drives the edit
    // debounce (heavy mesh rebuilds run at most 0.3 s after the last edit).
    void render(
        const std::vector<LandscapeTile>& tiles,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight,
        double nowSec);

private:
    // Vertex stream: field-space position (pre-camera; pos.z = raw ground
    // fieldY) + the composer-baked color.
    struct CyclopeanVertex {
        float pos[3];
        float color[4];
    };

    // VS uniforms: camera + depth normalization (28 bytes; the GLSL backend
    // requires the declared uniforms to sum exactly to the block size).
    struct CyclopeanVsParams {
        float view_size[2];
        float camera_offset[2];
        float z_range[2];
        float camera_zoom;
    };

    // Per-asset cache: the whole mesh of one asset, keyed by the content hash
    // (sorted on-nodes + mesh params). A mismatch marks the entry pending;
    // the stale mesh keeps rendering until the debounce fires.
    struct AssetCache {
        std::uint64_t contentHash = 0;
        bool pending = true;        // in the debounce queue (not built / rebuilding)
        double pendingSince = -1.0; // first frame this content went pending
        std::vector<CyclopeanVertex> stream;
        sg_buffer vbuf{SG_INVALID_ID};
        std::size_t vbufSize = 0;
    };

    void ensurePipeline();
    void destroyPipeline();
    void rebuildMesh(
        AssetCache& cache,
        const std::vector<const LandscapeTile*>& group,
        const topology_core::DiamondIsometry& iso,
        const CyclopeanParams& params);

    sg_pipeline pip{SG_INVALID_ID};
    sg_shader shd{SG_INVALID_ID};
    sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;

    std::unordered_map<std::string, CyclopeanParams> assets;
    std::unordered_map<std::string, AssetCache> caches;
};

} // namespace render_core
