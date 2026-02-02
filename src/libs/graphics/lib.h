#pragma once
#include <vector>

namespace Graphics {
    struct Vertex {
        float x, y;
        float r, g, b, a;
    };

    void init();
    void begin_frame(int width, int height);
    void draw_rects(const std::vector<Vertex>& vertices);
    void end_frame();
    
    // Legacy test
    void render_test_frame();
}
