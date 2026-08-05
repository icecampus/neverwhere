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

// Fill/edge palette of the flat tile (the defaults reproduce the ochre
// "Yellow 2D" look; a green variant paints the "Green 2D" layer).
struct FlatAtlasPalette {
    std::uint8_t fillR = 210, fillG = 170, fillB = 90;
    std::uint8_t edgeR = 120, edgeG = 90, edgeB = 40;
};

FlatAtlasImage generateFlatAtlas(const FlatAtlasPalette& palette = {});

// Helpers for smoke / debugging
int flatAtlasOpaquePixelCount(const FlatAtlasImage& atlas, int tileIndex);
