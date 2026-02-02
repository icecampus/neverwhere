#pragma once
#include <vector>

namespace Graphics {
    struct Vertex {
        float x, y;
        float r, g, b, a;
    };

    void init();
    void begin_frame(int width, int height);
    // Modified to accept view size for projection
    void draw_rects(const std::vector<Vertex>& vertices, int view_width, int view_height);
    void end_frame();
    
    // Legacy test
    void render_test_frame();
}
