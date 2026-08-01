#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

class LandBrush;

// CPU half of the ground/highground stitching: the contact-AO distance field
// and the orthographic sun frame used by the shadow map. No sokol here on
// purpose — the headless smoke test exercises the same code as the renderer.

// Distance beyond which the contact AO is fully gone (world cells). Also the
// scale the R8 field is normalized by, so the shader multiplies the texel back
// by this constant.
inline constexpr float kAoMaxDistanceCells = 4.0f;

// Distance from the highground footprint over the map bbox plus a margin.
// Texel 0 = at the footprint, 255 = kAoMaxDistanceCells or further away.
struct ContactAoField {
    int width = 0;
    int height = 0;
    float originX = 0.0f; // world x of the field's left edge (cell units)
    float originZ = 0.0f; // world z of the field's top edge
    float cellsPerTexel = 1.0f;
    std::vector<std::uint8_t> texels;

    bool empty() const { return texels.empty(); }
    float extentX() const { return static_cast<float>(width) * cellsPerTexel; }
    float extentZ() const { return static_cast<float>(height) * cellsPerTexel; }

    // Distance in cells at a world position (kAoMaxDistanceCells outside).
    float distanceAt(float worldX, float worldZ) const;
};

// Union of the footprints of the given brushes (cells whose tile type has a
// surface) turned into a chamfer distance transform. Null brushes are skipped;
// an empty footprint yields an empty field (the caller falls back to "no AO").
ContactAoField buildContactAoField(
    const LandBrush* const* brushes,
    int brushCount,
    int texelsPerCell,
    int marginCells);

// Orthographic sun frame fitted to an axis-aligned world box. row0/row1/row2
// map a world point (x, height, z) to [0,1] shadow coordinates: xy = the
// shadow-map uv, z = normalized distance from the light (near = small).
struct SunBasis {
    glm::vec3 dir{0.0f, 1.0f, 0.0f}; // towards the sun
    glm::vec3 right{1.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 0.0f, 1.0f};
    glm::vec4 row0{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec4 row1{0.0f, 1.0f, 0.0f, 0.0f};
    glm::vec4 row2{0.0f, 0.0f, 1.0f, 0.0f};
    bool valid = false;

    glm::vec3 project(const glm::vec3& worldPos) const;
};

SunBasis buildSunBasis(const glm::vec3& sunDir, const glm::vec3& boxMin, const glm::vec3& boxMax);

// Scalar-field height (the mesh py) -> world height in cell units.
//
// The iso projection is anisotropic in field space: one map unit spans
// (halfW, halfH) screen px while one height unit spans heightScale px. This is
// the y scale that makes the two projection rows orthogonal AND equal length,
// i.e. the height a viewer actually reads off the screen. Without it the cast
// shadow would not match the drawn silhouette.
float isoHeightToWorld(float halfW, float halfH, float heightScale);
