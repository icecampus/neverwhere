#pragma once

#include <cstdint>
#include <vector>

// Material ID (0-255, 0 = Empty)
using MaterialId = std::uint8_t;

enum class MaterialType : MaterialId {
    Empty = 0,
    Grass = 1,
    Sand = 2,
    Rock = 3,
};

struct MaterialMap {
    std::vector<MaterialId> data;
    int width = 0;
    int height = 0;

    void init(int w, int h, MaterialId fill = 0);
    MaterialId at(int x, int y) const;
    void set(int x, int y, MaterialId id);
    void fillRect(int x, int y, int w, int h, MaterialId id);
};

