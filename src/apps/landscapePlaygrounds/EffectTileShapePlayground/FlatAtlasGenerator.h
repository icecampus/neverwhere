#pragma once

#include <cstdint>
#include <vector>

// Procedural flat landscape atlas matching Grass layout (4x6 tiles of 64x64).
struct FlatAtlasImage {
    int width = 0;
    int height = 0;
    int cols = 4;
    int rows = 6;
    int tileSize = 64;
    std::vector<std::uint8_t> rgba; // width * height * 4
};

FlatAtlasImage generateFlatAtlas();

// Helpers for smoke / debugging
int flatAtlasOpaquePixelCount(const FlatAtlasImage& atlas, int tileIndex);
