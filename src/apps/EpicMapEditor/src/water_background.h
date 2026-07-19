#pragma once

#include "render_core/sokol_config.h"
#include "topology_core/camera2d.h"

// Water-caustics background for the editor map view — the old QSG CustomItem
// shader (qml/Workspace/MapView.qml background) restored in the unified render
// path. Drawn as a world-space quad (20000x20000, like the old item) before
// the world, so it pans/zooms together with the map.
class WaterBackground {
public:
    void init(sg_pixel_format depthFormat);
    void shutdown();

    void render(
        const topology_core::Camera2D& camera,
        int viewWidth,
        int viewHeight,
        float timeSeconds);

private:
    struct Vertex {
        float pos[2];
        float uv[2];
    };

    sg_pipeline pip{SG_INVALID_ID};
    sg_buffer vbuf{SG_INVALID_ID};
    sg_bindings bind{};
    sg_pixel_format depthFormat = SG_PIXELFORMAT_NONE;

    void ensurePipeline();
    void destroyPipeline();
};
