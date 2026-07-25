#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "render_core/sokol_config.h"

namespace render_core {

struct LineSegment {
    glm::vec2 p0{0.0f};
    glm::vec2 p1{0.0f};
    glm::vec4 color{1.0f};
};

// Untextured 1px colored lines for world overlays (grid, cell cursor, future gizmos).
// Input is in screen pixels — the caller applies camera transforms itself.
class OverlayRenderer {
public:
    // See LandscapeRenderer::init for the depthFormat contract.
    void init(sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL);
    void shutdown();

    void render(const std::vector<LineSegment>& lines, int viewWidth, int viewHeight);

private:
    struct Vertex {
        float pos[2];
        float color[4];
    };

    sg_pipeline pip{SG_INVALID_ID};
    sg_buffer vbuf{SG_INVALID_ID};
    std::size_t vbufSize = 0; // allocated bytes (the buffer grows on demand)
    sg_bindings bind{};
    sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;

    std::vector<Vertex> scratchVerts;

    void ensurePipeline();
    void destroyPipeline();
};

} // namespace render_core
