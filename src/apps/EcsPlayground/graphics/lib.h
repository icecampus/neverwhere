#pragma once
#include <vector>

// Minimal 2D-quad renderer over Sokol inside a Qt OpenGL context.
// Lives inside EcsPlayground — the only consumer of this GL glue;
// production rendering goes through src/libs/render_core instead.
namespace Graphics {
    struct Vertex {
        float x, y;
        float r, g, b, a;
    };

    void init();
    // Modified to accept view size for projection
    void draw_rects(const std::vector<Vertex>& vertices, int view_width, int view_height);
}
