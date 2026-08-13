// Fence pass (fence3d assets): baked ShapeML piece meshes instanced per
// FenceLandscape piece object, in the shared world render. Sibling of the
// cyclopean pass: baked vertex colors (no textures, no FS uniforms), the
// camera applied in the VS (the cached vertex stream survives pan/zoom),
// pos.z = ground fieldY + height lift (liftedGroundY, depth-levels model)
// normalized per frame via the z_range uniform. The tool's transient ghost
// preview (stroke plan / move preview) rides along in a second, depth-ignored
// translucent draw with its own buffer (sokol: one sg_update_buffer per
// buffer per frame).
//
// The pass needs a depth attachment: with SG_PIXELFORMAT_NONE at init it is
// disabled (same contract as the cliff/cyclopean passes).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include <fence_core/fence_mesh.h>

#include "render_core/sokol_config.h"

#include "topology_core/camera2d.h"
#include "topology_core/diamond_isometry.h"

namespace render_core {

// One fence piece of the frame (FenceLandscape layer object, annotated by the
// frame builder): a post (kind 0, 1 cell) or a section (kind 1, `length`
// cells along `axis`). fenceId/corner are derived by the frame builder's
// whole-layer FenceModel so the tool and every renderer agree on them.
struct FencePiece {
    glm::ivec2 cell{0, 0};
    int kind = 0;            // 0 = post, 1 = section
    glm::ivec2 axis{0, 0};   // section: signed unit axis; post: (0,0)
    int length = 1;          // section: covered cells (1..2); post: 1
    std::string assetUuid;
    int fenceId = -1;        // whole-layer component id (selection tint)
    bool corner = false;     // post: taller corner/gate mesh
};

class FenceRenderer {
public:
    // depthFormat must match the pass the renderer draws into; the pass is
    // disabled when depthFormat == SG_PIXELFORMAT_NONE.
    void init(sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL);
    void shutdown();

    // Register/update a fence3d asset: the bundle dir holding the
    // conventional fence_{post,corner,section2,section3}.obj meshes and the
    // vertical lift of 1 m in screen points. Cheap (the meshes load lazily on
    // the first frame that instances the asset).
    void ensureFenceAsset(
        const std::string& assetUuid,
        const std::filesystem::path& meshDir,
        float metersToPoints);

    bool hasAsset(const std::string& assetUuid) const {
        return assets.find(assetUuid) != assets.end();
    }

    // Draw the committed pieces (per-asset vertex streams, rebuilt only when
    // the content key changes — fences are light, no debounce needed) and
    // then the ghost preview on top (green = applicable, red = rejected).
    void render(
        const std::vector<FencePiece>& pieces,
        const std::vector<FencePiece>& ghost,
        bool ghostValid,
        int selectedFenceId,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight);

private:
    // Vertex stream: field-space position (pre-camera; pos.z = ground fieldY
    // + height lift) + the baked color (same layout as CyclopeanVertex).
    struct FenceVertex {
        float pos[3];
        float color[4];
    };

    // VS uniforms: camera + depth normalization (28 bytes; the GLSL backend
    // requires the declared uniforms to sum exactly to the block size).
    struct FenceVsParams {
        float view_size[2];
        float camera_offset[2];
        float z_range[2];
        float camera_zoom;
    };

    struct AssetEntry {
        std::filesystem::path meshDir;
        float metersToPoints = fence_core::kFenceMetersToPoints;
        fence_core::FenceMeshSet meshes;
        bool meshesTried = false; // load attempted (success lives in meshes.ok)
    };

    struct AssetCache {
        std::vector<FenceVertex> stream;
        sg_buffer vbuf{SG_INVALID_ID};
        std::size_t vbufSize = 0;
    };

    void ensurePipeline();
    void destroyPipeline();
    void rebuildStreams(
        const std::vector<FencePiece>& pieces,
        int selectedFenceId,
        const topology_core::DiamondIsometry& iso);

    sg_pipeline pip{SG_INVALID_ID};
    sg_pipeline ghostPip{SG_INVALID_ID};
    sg_shader shd{SG_INVALID_ID};
    sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;

    std::unordered_map<std::string, AssetEntry> assets;
    std::unordered_map<std::string, AssetCache> caches;
    std::uint64_t contentHash = 0; // global key over pieces + selection + lifts

    sg_buffer ghostVbuf{SG_INVALID_ID};
    std::size_t ghostVbufSize = 0;
};

} // namespace render_core
