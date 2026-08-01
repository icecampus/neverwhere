#include "MaterialTypes.h"

#include <algorithm>

void MaterialMap::init(int w, int h, MaterialId fill) {
    width = std::max(0, w);
    height = std::max(0, h);
    data.clear();
    data.resize((size_t)width * (size_t)height, fill);
}

MaterialId MaterialMap::at(int x, int y) const {
    if (x < 0 || y < 0 || x >= width || y >= height) return 0;
    const size_t idx = (size_t)y * (size_t)width + (size_t)x;
    return data[idx];
}

void MaterialMap::set(int x, int y, MaterialId id) {
    if (x < 0 || y < 0 || x >= width || y >= height) return;
    const size_t idx = (size_t)y * (size_t)width + (size_t)x;
    data[idx] = id;
}

void MaterialMap::fillRect(int x, int y, int w, int h, MaterialId id) {
    if (w <= 0 || h <= 0) return;
    for (int yy = 0; yy < h; yy++) {
        for (int xx = 0; xx < w; xx++) {
            set(x + xx, y + yy, id);
        }
    }
}

