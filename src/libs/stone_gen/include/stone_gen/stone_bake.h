// CPU texture baker: rasterizes the stone mesh in UV space and evaluates the
// SDF material per texel — albedo (rgb) + AO (a) and object-space normal —
// plus OBJ/PNG writers. No Qt/GPU: bakes are testable and exportable anywhere.
#pragma once

#include "stone_gen/stone_mesh.h"

#include <cstdint>
#include <string>
#include <vector>

namespace stone_gen {

struct BakeParams {
    int textureSize = 512;
    int aoTaps = 8;
    int dilationPx = 8; // edge padding against mip bleeding
};

struct BakedTextures {
    int size = 0;
    std::vector<std::uint8_t> albedo; // RGBA: rgb = albedo, a = AO
    std::vector<std::uint8_t> normal; // RGBA: object-space normal (0.5+0.5n)
    double bakeMs = 0.0;
};

BakedTextures bakeTextures(const StoneSdf& sdf, const StoneMesh& mesh,
    const BakeParams& params = BakeParams{});

// PNG: rgba buffer, row 0 = uv v0 (the baker's own convention).
bool writePng(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgba);
// OBJ: v/vt/vn; vt is V-flipped into the standard OBJ convention (v0 = image
// bottom), so the pair mesh.obj + *.png reads correctly in generic viewers.
bool writeObj(const std::string& path, const StoneMesh& mesh);

} // namespace stone_gen
