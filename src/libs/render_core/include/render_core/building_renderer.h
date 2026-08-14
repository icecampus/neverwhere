#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <glm/glm.hpp>

#include "render_core/sokol_config.h"
#include "render_core/scene_stitch.h"

#include "topology_core/camera2d.h"
#include "topology_core/diamond_isometry.h"

namespace render_core {

// One placed building3d object. Origin cell is the footprint centre (odd
// sizes) — the mesh is already scaled into cell units and sits on y = 0.
struct BuildingInstance {
    glm::ivec2 cell{0, 0};
    std::string assetUuid;
};

struct BuildingParams {
    std::filesystem::path modelPath;
    std::filesystem::path albedoPath;
    int footprintWidth = 3;
    int footprintHeight = 3;
    float heightScale = 96.0f;
    float yawDegrees = 0.0f;
    float scale = 1.0f; // uniform multiplier on top of the footprint fit
};

class BuildingRenderer {
public:
    void init(sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL);
    void shutdown();

    void ensureBuildingAsset(const std::string& assetUuid, const BuildingParams& params);

    void render(
        const std::vector<BuildingInstance>& instances,
        const topology_core::DiamondIsometry& iso,
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight,
        const SceneStitchSettings& stitch);

    struct VsParams {
        float view_size[2];
        float camera_offset[2];
        float z_range[2];
        float camera_zoom;
        float height_scale;
        float half_size[2];
    };
    struct FsParams {
        float sun_dir[4];
        float ambient;
        float diffuse;
    };

private:
    struct Vertex {
        float pos[3];
        float nrm[3];
        float uv[2];
    };
    struct Instance {
        float cell[2];
    };
    struct AssetGpu {
        BuildingParams params;
        sg_buffer vbuf{SG_INVALID_ID};
        sg_buffer ibuf{SG_INVALID_ID};
        sg_buffer instBuf{SG_INVALID_ID};
        std::size_t instBufSize = 0;
        sg_image albedo{SG_INVALID_ID};
        sg_view albedoView{SG_INVALID_ID};
        int indexCount = 0;
        bool loaded = false;
        bool loadFailed = false;
    };

    sg_pipeline pip{SG_INVALID_ID};
    sg_shader shd{SG_INVALID_ID};
    sg_sampler smp{SG_INVALID_ID};
    sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;
    std::unordered_map<std::string, AssetGpu> assets;
    std::vector<Instance> scratchInst;

    void ensurePipeline();
    void destroyPipeline();
    void loadAsset(AssetGpu& gpu);
    void destroyAsset(AssetGpu& gpu);
};

} // namespace render_core
