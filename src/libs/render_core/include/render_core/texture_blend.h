// Texture-2D landscape layer: recover the per-node texture identity from the
// stored per-cell tiles and derive the fan-tessellation blend data (the
// SDFGeneratedLandscape "Texture 2D" layer contract). Pure CPU, no GPU.
#pragma once

#include <array>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "render_core/landscape_renderer.h"

namespace render_core {

// One texture-layer cell ready for fan tessellation: the distinct texture
// assets of its on corner nodes (first-seen in corner order
// Left/Up/Right/Down), the one-hot corner weights over those candidates and
// the per-corner on/off fill. Mirrors LandBrush::cellTextureBlend.
struct TextureBlendCell {
    glm::ivec2 cell{0, 0};
    int candidateCount = 0;
    std::array<std::string, 4> candidateUuids{};
    std::array<glm::vec4, 4> cornerWeights{};
    std::array<float, 4> cornerFill{};
};

// The map persists per-cell tiles only (the 4-bit corner mask inside
// tileIndex + one assetUuid per cell), so the per-node texture tags of the
// playground brush are reconstructed by voting: every tile votes with its
// assetUuid for each of its on corner nodes; the node takes the majority
// texture, ties go to the candidate whose FIRST vote came latest in tile
// order (approximates the playground's last-paint-wins tag rewrite, since
// repainted cells are re-appended to the layer). Deterministic for a given
// tile vector. Cells without a single on-node are dropped.
std::vector<TextureBlendCell> buildTextureBlendCells(const std::vector<LandscapeTile>& tiles);

} // namespace render_core
