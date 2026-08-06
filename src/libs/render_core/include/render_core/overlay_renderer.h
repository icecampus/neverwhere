#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "render_core/sokol_config.h"

namespace render_core {

struct LineSegment {
    glm::vec2 p0{0.0f};
    glm::vec2 p1{0.0f};
    glm::vec4 color{1.0f};
    // Normalized depth of each endpoint in the scene's z convention (see
    // WorldRenderer::render). Only the grid channel consumes it; the top
    // channel ignores depth entirely, so its default is the near plane.
    float depth0 = 0.0f;
    float depth1 = 0.0f;
};

// Untextured 1px colored lines for world overlays (grid, cell cursor, future gizmos).
// Input is in screen pixels — the caller applies camera transforms itself.
// Two channels: the grid pass (the ground plane of the scene: it depth-tests AND
// writes, so the 3D world overdraws it where it rises above the plane and stays
// hidden where it hangs below — the water-plane reading) and the always-on-top
// pass (cell cursor) — separate buffers, because sokol allows only one
// sg_update_buffer per buffer per frame.
class OverlayRenderer {
public:
    // See LandscapeRenderer::init for the depthFormat contract.
    void init(sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL);
    void shutdown();

    // Primary overlay pass (the grid — depth-tested and depth-writing when the
    // pass has a depth attachment, so it acts as the scene's ground plane).
    void render(const std::vector<LineSegment>& lines, int viewWidth, int viewHeight);
    // Always-on-top overlay pass (the cell cursor).
    void renderTop(const std::vector<LineSegment>& lines, int viewWidth, int viewHeight);

private:
    struct Vertex {
        float pos[3]; // x, y in screen pixels; z = normalized depth
        float color[4];
    };

    struct Channel {
        sg_buffer vbuf{SG_INVALID_ID};
        std::size_t vbufSize = 0; // allocated bytes (the buffer grows on demand)
        sg_bindings bind{};
    };

    sg_pipeline pip{SG_INVALID_ID};      // no depth test/write (cursor, and the
                                         // fallback when the pass has no depth)
    sg_pipeline pipDepth{SG_INVALID_ID}; // grid: LESS_EQUAL + depth write
    Channel channelA; // grid
    Channel channelB; // cursor (always on top)
    sg_pixel_format depthFormat = SG_PIXELFORMAT_DEPTH_STENCIL;

    std::vector<Vertex> scratchVerts;

    void ensurePipeline();
    void destroyPipeline();
    void renderLines(
        Channel& channel,
        sg_pipeline pipeline,
        const std::vector<LineSegment>& lines,
        int viewWidth,
        int viewHeight);
};

} // namespace render_core
