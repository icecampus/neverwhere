#include "Landscape3dRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <stdexcept>
#include <unordered_map>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <topology_core/staggered_isometry.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace {

constexpr float kPi = 3.14159265358979323846f;

enum class FaceKind {
    Top,
    Side,
    Line,
};

enum class QuadWinding {
    Forward,
    Reverse,
};

struct CliffBoundaryEdge {
    glm::vec3 topA{0.0f};
    glm::vec3 topB{0.0f};
    glm::vec3 bottomA{0.0f};
    glm::vec3 bottomB{0.0f};
    glm::vec3 normal{0.0f, 0.0f, 1.0f};
};

float radians(float deg) {
    return deg * kPi / 180.0f;
}

glm::vec4 earthColorForLevel(int level, int maxLevel) {
    const float t = (maxLevel > 0) ? ((float)level / (float)maxLevel) : 0.0f;
    return {
        0.34f + 0.08f * t,
        0.18f + 0.05f * t,
        0.09f + 0.025f * t,
        1.0f
    };
}

glm::vec4 materialTint(TerrainMaterial material);

float hash11(float value) {
    const float n = std::sin(value * 127.1f) * 43758.5453f;
    return n - std::floor(n);
}

float hash21(int x, int z, float salt) {
    return hash11((float)x * 17.17f + (float)z * 91.31f + salt);
}

glm::vec4 variedGrassTint(TerrainMaterial material, int x, int z, bool enabled, float layerBias = 0.0f) {
    glm::vec4 tint = materialTint(material);
    if (!enabled) {
        return tint;
    }

    const float warm = hash21(x, z, 3.0f) - 0.5f;
    const float value = hash21(x, z, 11.0f) - 0.5f;
    const float patch = hash21(x, z, 17.0f);
    const float layer = std::clamp(layerBias, -1.0f, 1.0f);
    const float shade = 0.78f + patch * 0.36f + layer * 0.10f;
    tint.r *= shade * (0.92f + warm * 0.18f);
    tint.g *= shade * (0.98f + value * 0.16f);
    tint.b *= shade * (0.82f - warm * 0.14f);
    return tint;
}

glm::vec2 variedUv(const glm::vec2& uv, int x, int z, bool enabled) {
    if (!enabled) {
        return uv;
    }

    glm::vec2 result = uv;
    if (hash21(x, z, 23.0f) > 0.5f) {
        result.x = 1.0f - result.x;
    }
    if (hash21(x, z, 29.0f) > 0.5f) {
        result.y = 1.0f - result.y;
    }
    const glm::vec2 offset{
        std::floor(hash21(x, z, 37.0f) * 4.0f) * 0.25f,
        std::floor(hash21(x, z, 41.0f) * 4.0f) * 0.25f,
    };
    return result + offset;
}

glm::vec4 earthVertexColor(
    int level,
    int maxLevel,
    const glm::vec3& position,
    float topHeight,
    bool sideGradient) {

    glm::vec4 color = earthColorForLevel(level, maxLevel);
    if (!sideGradient) {
        return color;
    }

    const float vertical = (topHeight > 0.001f) ? std::clamp(position.y / topHeight, 0.0f, 1.0f) : 0.0f;
    const float noise = hash11(position.x * 9.3f + position.z * 13.1f + position.y * 5.7f) - 0.5f;
    const float shade = 0.42f + vertical * 0.78f + noise * 0.18f;
    color.r *= shade;
    color.g *= shade;
    color.b *= shade;
    return color;
}

glm::vec4 materialTint(TerrainMaterial material) {
    switch (material) {
    case TerrainMaterial::Sand:
        return {0.88f, 0.78f, 0.50f, 1.0f};
    case TerrainMaterial::Rock:
        return {0.60f, 0.62f, 0.58f, 1.0f};
    case TerrainMaterial::Grass:
    default:
        return {0.95f, 1.0f, 0.88f, 1.0f};
    }
}

glm::vec2 sideSurfaceUv(const glm::vec3& position, const glm::vec3& normal) {
    constexpr float kSideUvHorizontalScale = 10.0f;
    constexpr float kSideUvVerticalScale = 14.0f;

    const glm::vec3 up{0.0f, 1.0f, 0.0f};
    glm::vec3 tangent = glm::cross(up, normal);
    tangent.y = 0.0f;
    if (glm::length(tangent) <= 0.0001f) {
        return {position.x * kSideUvHorizontalScale, position.z * kSideUvHorizontalScale};
    }

    tangent = glm::normalize(tangent);
    return {glm::dot(position, tangent) * kSideUvHorizontalScale, position.y * kSideUvVerticalScale};
}

glm::vec3 cubeCorner(int x, int y, int z, float cubeSize, int gridSize) {
    const float half = (float)gridSize * cubeSize * 0.5f;
    return {
        (float)x * cubeSize - half,
        (float)y * cubeSize,
        (float)z * cubeSize - half
    };
}

glm::vec3 safeNormalize(const glm::vec3& value, const glm::vec3& fallback) {
    const float len = glm::length(value);
    return len > 0.0001f ? value / len : fallback;
}

glm::vec3 quadNormalForWinding(
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    QuadWinding winding,
    const glm::vec3& fallback) {

    const glm::vec3 normal = winding == QuadWinding::Forward
        ? glm::cross(b - a, c - a)
        : glm::cross(c - a, b - a);
    return safeNormalize(normal, fallback);
}

QuadWinding windingForExpectedNormal(
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec3& expectedNormal) {

    const glm::vec3 expected = safeNormalize(expectedNormal, {0.0f, 1.0f, 0.0f});
    const glm::vec3 forward = quadNormalForWinding(a, b, c, QuadWinding::Forward, expected);
    return glm::dot(forward, expected) >= 0.0f ? QuadWinding::Forward : QuadWinding::Reverse;
}

void pushQuad(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec3& d,
    const glm::vec4& color,
    FaceKind faceKind,
    QuadWinding winding = QuadWinding::Forward) {

    const std::uint32_t base = (std::uint32_t)vertices.size();
    const glm::vec3 fallbackNormal = faceKind == FaceKind::Top ? glm::vec3{0.0f, 1.0f, 0.0f} : glm::vec3{0.0f, 0.0f, 1.0f};
    const glm::vec3 normal = quadNormalForWinding(a, b, c, winding, fallbackNormal);
    const float kind = (float)(int)faceKind;
    const bool sideFace = faceKind == FaceKind::Side;
    vertices.push_back({a, normal, color, sideFace ? sideSurfaceUv(a, normal) : glm::vec2{0.0f, 0.0f}, kind});
    vertices.push_back({b, normal, color, sideFace ? sideSurfaceUv(b, normal) : glm::vec2{1.0f, 0.0f}, kind});
    vertices.push_back({c, normal, color, sideFace ? sideSurfaceUv(c, normal) : glm::vec2{0.0f, 1.0f}, kind});
    vertices.push_back({d, normal, color, sideFace ? sideSurfaceUv(d, normal) : glm::vec2{1.0f, 1.0f}, kind});

    if (winding == QuadWinding::Forward) {
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 3);
        indices.push_back(base + 2);
    } else {
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    }
}

void pushQuadOriented(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec3& d,
    const glm::vec3& expectedNormal,
    const glm::vec4& color,
    FaceKind faceKind) {

    pushQuad(vertices, indices, a, b, c, d, color, faceKind, windingForExpectedNormal(a, b, c, expectedNormal));
}

void pushTriangle(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    glm::vec3 a,
    glm::vec3 b,
    glm::vec3 c,
    glm::vec2 uvA,
    glm::vec2 uvB,
    glm::vec2 uvC,
    const glm::vec4& color,
    FaceKind faceKind) {

    glm::vec3 normal = glm::normalize(glm::cross(b - a, c - a));
    if (normal.y < 0.0f) {
        std::swap(b, c);
        std::swap(uvB, uvC);
        normal = glm::normalize(glm::cross(b - a, c - a));
    }

    const std::uint32_t base = (std::uint32_t)vertices.size();
    const float kind = (float)(int)faceKind;
    vertices.push_back({a, normal, color, uvA, kind});
    vertices.push_back({b, normal, color, uvB, kind});
    vertices.push_back({c, normal, color, uvC, kind});
    indices.push_back(base + 0);
    indices.push_back(base + 1);
    indices.push_back(base + 2);
}

void pushLine(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    const glm::vec3& a,
    const glm::vec3& b) {
    const glm::vec4 lineColor{0.045f, 0.075f, 0.035f, 1.0f};
    const glm::vec3 n{0.0f, 1.0f, 0.0f};
    const float kind = (float)(int)FaceKind::Line;
    vertices.push_back({a, n, lineColor, {0.0f, 0.0f}, kind});
    vertices.push_back({b, n, lineColor, {0.0f, 0.0f}, kind});
}

void pushBoxWireframe(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    const glm::vec3& p000,
    const glm::vec3& p100,
    const glm::vec3& p010,
    const glm::vec3& p110,
    const glm::vec3& p001,
    const glm::vec3& p101,
    const glm::vec3& p011,
    const glm::vec3& p111) {

    pushLine(vertices, p000, p100);
    pushLine(vertices, p100, p110);
    pushLine(vertices, p110, p010);
    pushLine(vertices, p010, p000);

    pushLine(vertices, p001, p101);
    pushLine(vertices, p101, p111);
    pushLine(vertices, p111, p011);
    pushLine(vertices, p011, p001);

    pushLine(vertices, p000, p001);
    pushLine(vertices, p100, p101);
    pushLine(vertices, p010, p011);
    pushLine(vertices, p110, p111);
}

void addTileStat(Landscape3dTileStats& stats, LandscapeTileType type) {
    switch (type) {
    case LandscapeTileType::Full:
        stats.full++;
        break;
    case LandscapeTileType::RightCorner:
    case LandscapeTileType::LeftCorner:
    case LandscapeTileType::UpCorner:
    case LandscapeTileType::DownCorner:
        stats.corners++;
        break;
    case LandscapeTileType::DownLack:
    case LandscapeTileType::UpLack:
    case LandscapeTileType::RightLack:
    case LandscapeTileType::LeftLack:
        stats.lacks++;
        break;
    case LandscapeTileType::RightDownLine:
    case LandscapeTileType::LeftDownLine:
    case LandscapeTileType::RightUpLine:
    case LandscapeTileType::LeftUpLine:
        stats.lines++;
        break;
    case LandscapeTileType::UpAndDownCorners:
    case LandscapeTileType::LeftRightCorners:
        stats.opposites++;
        break;
    case LandscapeTileType::Unknown:
    default:
        stats.unknown++;
        break;
    }
}

glm::vec4 tileDebugColor(LandscapeTileType type, TerrainMaterial material) {
    switch (type) {
    case LandscapeTileType::Full:
        return materialTint(material);
    case LandscapeTileType::RightCorner:
    case LandscapeTileType::LeftCorner:
    case LandscapeTileType::UpCorner:
    case LandscapeTileType::DownCorner:
        return {0.72f, 1.00f, 0.58f, 1.0f};
    case LandscapeTileType::DownLack:
    case LandscapeTileType::UpLack:
    case LandscapeTileType::RightLack:
    case LandscapeTileType::LeftLack:
        return {0.98f, 0.94f, 0.55f, 1.0f};
    case LandscapeTileType::RightDownLine:
    case LandscapeTileType::LeftDownLine:
    case LandscapeTileType::RightUpLine:
    case LandscapeTileType::LeftUpLine:
        return {0.56f, 0.84f, 1.0f, 1.0f};
    case LandscapeTileType::UpAndDownCorners:
    case LandscapeTileType::LeftRightCorners:
        return {0.88f, 0.62f, 1.0f, 1.0f};
    case LandscapeTileType::Unknown:
    default:
        return {0.22f, 0.22f, 0.22f, 1.0f};
    }
}

void pushTileWireframe(
    std::vector<Landscape3dRenderer::TerrainVertex>& lineVertices,
    const std::array<glm::vec3, 5>& points) {

    pushLine(lineVertices, points[0], points[1]);
    pushLine(lineVertices, points[1], points[2]);
    pushLine(lineVertices, points[2], points[3]);
    pushLine(lineVertices, points[3], points[0]);
    pushLine(lineVertices, points[4], points[0]);
    pushLine(lineVertices, points[4], points[1]);
    pushLine(lineVertices, points[4], points[2]);
    pushLine(lineVertices, points[4], points[3]);
}

glm::vec3 horizontalNormalFromCenter(const glm::vec3& center, const glm::vec3& a, const glm::vec3& b) {
    glm::vec3 normal = ((a + b) * 0.5f) - center;
    normal.y = 0.0f;
    const float len = glm::length(normal);
    return len > 0.0001f ? normal / len : glm::vec3{0.0f, 0.0f, 1.0f};
}

float valleyHeightStep(float cellWidth, const Landscape3dRenderParams& params) {
    return std::max(0.01f, params.heightStepInCubes) * cellWidth;
}

float bandBaseHeight(std::uint8_t level, float heightStep) {
    return (float)std::max(0, (int)level - 1) * heightStep;
}

float bandTopHeight(std::uint8_t level, float heightStep) {
    return (float)level * heightStep;
}

void addZeroLayerFill(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    int x,
    int z,
    int grid,
    const Landscape3dRenderParams& params) {

    topology_core::StaggeredIsometry iso;
    iso.dims.cellWidth = std::max(0.1f, params.cubeSize);
    iso.dims.aspectRatio = Landscape3dCamera::editorGroundAspectRatio;

    const float cellWidth = iso.dims.cellSize().x;
    const float cellDepth = iso.dims.cellSize().y;
    const glm::vec2 origin = iso.mapToField({grid / 2, grid / 2});
    const glm::vec2 center2 = iso.mapToField({x, z}) - origin;
    const float y = -0.002f * cellWidth;

    const glm::vec3 left{center2.x - cellWidth * 0.5f, y, center2.y};
    const glm::vec3 up{center2.x, y, center2.y - cellDepth * 0.5f};
    const glm::vec3 right{center2.x + cellWidth * 0.5f, y, center2.y};
    const glm::vec3 down{center2.x, y, center2.y + cellDepth * 0.5f};
    const glm::vec4 topColor = variedGrassTint(TerrainMaterial::Grass, x, z, params.grassVariation, -0.25f);
    const std::uint32_t base = (std::uint32_t)vertices.size();
    pushQuadOriented(vertices, indices, left, up, down, right, {0.0f, 1.0f, 0.0f}, topColor, FaceKind::Top);
    vertices[base + 0].uv = variedUv({0.0f, 0.0f}, x, z, params.grassVariation);
    vertices[base + 1].uv = variedUv({0.5f, 0.0f}, x, z, params.grassVariation);
    vertices[base + 2].uv = variedUv({0.0f, 0.5f}, x, z, params.grassVariation);
    vertices[base + 3].uv = variedUv({0.5f, 0.5f}, x, z, params.grassVariation);
}

std::array<glm::vec3, 5> cellValleyPoints(
    int x,
    int z,
    int grid,
    const Landscape3dRenderParams& params,
    LandscapeTileType type,
    float baseHeight,
    float heightStep) {

    const LandscapeValleyGeometry geometry = valleyGeometryForTile(type);
    topology_core::StaggeredIsometry iso;
    iso.dims.cellWidth = std::max(0.1f, params.cubeSize);
    iso.dims.aspectRatio = Landscape3dCamera::editorGroundAspectRatio;

    const float cellWidth = iso.dims.cellSize().x;
    const float cellDepth = iso.dims.cellSize().y;
    const glm::vec2 origin = iso.mapToField({grid / 2, grid / 2});
    const glm::vec2 center2 = iso.mapToField({x, z}) - origin;

    const std::array<glm::vec2, 5> offsets{
        glm::vec2{-cellWidth * 0.5f, 0.0f},
        glm::vec2{0.0f, -cellDepth * 0.5f},
        glm::vec2{cellWidth * 0.5f, 0.0f},
        glm::vec2{0.0f, cellDepth * 0.5f},
        glm::vec2{0.0f, 0.0f},
    };

    std::array<glm::vec3, 5> points{};
    for (int i = 0; i < 5; i++) {
        const glm::vec2 p = center2 + offsets[i];
        points[i] = {p.x, baseHeight + geometry.heights[i] * heightStep, p.y};
    }
    return points;
}

void addValleyTileObject(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    std::vector<Landscape3dRenderer::TerrainVertex>& lineVertices,
    int x,
    int z,
    int grid,
    const TerrainScene& scene,
    const Landscape3dRenderParams& params,
    LandscapeTileType type,
    float baseHeight,
    float heightStep,
    bool addSides,
    bool addDebugWireframe,
    const std::array<bool, 4>* sideMask = nullptr) {

    const LandscapeValleyGeometry geometry = valleyGeometryForTile(type);
    if (geometry.triangleCount <= 0) return;

    const std::array<glm::vec2, 5> uvs{
        glm::vec2{0.0f, 0.5f},
        glm::vec2{0.5f, 0.0f},
        glm::vec2{1.0f, 0.5f},
        glm::vec2{0.5f, 1.0f},
        glm::vec2{0.5f, 0.5f},
    };

    const std::array<glm::vec3, 5> points = cellValleyPoints(x, z, grid, params, type, baseHeight, heightStep);

    const glm::vec4 topColor = variedGrassTint(scene.materialAt(x, z), x, z, params.grassVariation);
    for (int i = 0; i < geometry.triangleCount; i++) {
        const std::array<std::uint8_t, 3>& tri = geometry.triangles[i];
        pushTriangle(
            vertices,
            indices,
            points[tri[0]],
            points[tri[1]],
            points[tri[2]],
            variedUv(uvs[tri[0]], x, z, params.grassVariation),
            variedUv(uvs[tri[1]], x, z, params.grassVariation),
            variedUv(uvs[tri[2]], x, z, params.grassVariation),
            topColor,
            FaceKind::Top);
    }

    if (addSides) {
        const glm::vec4 sideColor = earthColorForLevel(scene.columnHeightAt(x, z), std::max(1, scene.maxHeight()));
        for (int i = 0; i < 4; i++) {
            if (sideMask != nullptr && !(*sideMask)[i]) {
                continue;
            }

            const int next = (i + 1) % 4;
            if (geometry.heights[i] <= 0.0f && geometry.heights[next] <= 0.0f) {
                continue;
            }

            glm::vec3 baseA = points[i];
            glm::vec3 baseB = points[next];
            baseA.y = baseHeight;
            baseB.y = baseHeight;
            const glm::vec3 normal = horizontalNormalFromCenter(points[4], points[i], points[next]);
            const std::uint32_t base = (std::uint32_t)vertices.size();
            pushQuadOriented(vertices, indices, baseA, baseB, points[i], points[next], normal, sideColor, FaceKind::Side);
            vertices[base + 0].color = earthVertexColor(scene.columnHeightAt(x, z), std::max(1, scene.maxHeight()), baseA, baseHeight + heightStep, params.sideGradient);
            vertices[base + 1].color = earthVertexColor(scene.columnHeightAt(x, z), std::max(1, scene.maxHeight()), baseB, baseHeight + heightStep, params.sideGradient);
            vertices[base + 2].color = earthVertexColor(scene.columnHeightAt(x, z), std::max(1, scene.maxHeight()), points[i], baseHeight + heightStep, params.sideGradient);
            vertices[base + 3].color = earthVertexColor(scene.columnHeightAt(x, z), std::max(1, scene.maxHeight()), points[next], baseHeight + heightStep, params.sideGradient);
        }
    }

    if (addDebugWireframe) {
        pushTileWireframe(lineVertices, points);
    }
}

glm::ivec2 neighbourCellForEdge(int x, int z, int edge) {
    const bool oddRow = (z & 1) != 0;
    if (oddRow) {
        switch (edge) {
        case 0: return {x, z - 1};
        case 1: return {x + 1, z - 1};
        case 2: return {x + 1, z + 1};
        case 3: return {x, z + 1};
        default: return {x, z};
        }
    }

    switch (edge) {
    case 0: return {x - 1, z - 1};
    case 1: return {x, z - 1};
    case 2: return {x, z + 1};
    case 3: return {x - 1, z + 1};
    default: return {x, z};
    }
}

std::array<glm::vec3, 4> cellDiamondPoints(int x, int z, int grid, const Landscape3dRenderParams& params, float y) {
    topology_core::StaggeredIsometry iso;
    iso.dims.cellWidth = std::max(0.1f, params.cubeSize);
    iso.dims.aspectRatio = Landscape3dCamera::editorGroundAspectRatio;

    const float cellWidth = iso.dims.cellSize().x;
    const float cellDepth = iso.dims.cellSize().y;
    const glm::vec2 origin = iso.mapToField({grid / 2, grid / 2});
    const glm::vec2 center2 = iso.mapToField({x, z}) - origin;

    return {
        glm::vec3{center2.x - cellWidth * 0.5f, y, center2.y},
        glm::vec3{center2.x, y, center2.y - cellDepth * 0.5f},
        glm::vec3{center2.x + cellWidth * 0.5f, y, center2.y},
        glm::vec3{center2.x, y, center2.y + cellDepth * 0.5f},
    };
}

bool cellHasLevel(const TerrainScene& scene, int grid, int x, int z, std::uint8_t level) {
    if (x < 0 || z < 0 || x >= grid || z >= grid) {
        return false;
    }
    return tileTypeHasSurface(scene.tileTypeAtLevel(x, z, level));
}

LandscapeTileType cellBandType(const TerrainScene& scene, int grid, int x, int z, std::uint8_t level) {
    if (x < 0 || z < 0 || x >= grid || z >= grid) {
        return LandscapeTileType::Unknown;
    }
    return scene.tileTypeAtLevel(x, z, level);
}

std::array<bool, 4> contourSideMaskForCell(
    const TerrainScene& scene,
    int grid,
    int x,
    int z,
    Landscape3dTileStats* stats = nullptr) {

    std::array<bool, 4> mask{};
    const bool hasHigh = cellHasLevel(scene, grid, x, z, 2);
    for (int edge = 0; edge < 4; edge++) {
        const glm::ivec2 neighbour = neighbourCellForEdge(x, z, edge);
        const bool neighbourHasLow = cellHasLevel(scene, grid, neighbour.x, neighbour.y, 1);
        const bool neighbourHasHigh = cellHasLevel(scene, grid, neighbour.x, neighbour.y, 2);
        mask[edge] = hasHigh && neighbourHasLow && !neighbourHasHigh;
        if (mask[edge] && stats != nullptr) {
            stats->contourSmoothEdges++;
        }
    }
    return mask;
}

std::array<bool, 4> bandBoundarySideMask(
    const TerrainScene& scene,
    int grid,
    int x,
    int z,
    std::uint8_t level) {

    std::array<bool, 4> mask{};
    for (int edge = 0; edge < 4; edge++) {
        const glm::ivec2 neighbour = neighbourCellForEdge(x, z, edge);
        mask[edge] = !cellHasLevel(scene, grid, neighbour.x, neighbour.y, level);
    }
    return mask;
}

bool contourTopPointsForCell(
    const TerrainScene& scene,
    const Landscape3dRenderParams& params,
    int grid,
    int x,
    int z,
    float heightStep,
    std::array<glm::vec3, 5>& points) {

    const LandscapeTileType highType = cellBandType(scene, grid, x, z, 2);
    if (tileTypeHasSurface(highType)) {
        points = cellValleyPoints(x, z, grid, params, highType, bandBaseHeight(2, heightStep), heightStep);
        return true;
    }

    const LandscapeTileType lowType = cellBandType(scene, grid, x, z, 1);
    if (tileTypeHasSurface(lowType)) {
        points = cellValleyPoints(x, z, grid, params, lowType, bandBaseHeight(1, heightStep), heightStep);
        return true;
    }

    return false;
}

std::int64_t contourEndpointKey(const glm::vec3& point) {
    const std::int64_t x = (std::int64_t)std::llround(point.x * 10000.0f);
    const std::int64_t z = (std::int64_t)std::llround(point.z * 10000.0f);
    return (x << 32) ^ (z & 0xffffffffLL);
}

int countContourChains(const std::vector<CliffBoundaryEdge>& edges) {
    std::unordered_map<std::int64_t, std::vector<int>> endpointToEdges;
    endpointToEdges.reserve(edges.size() * 2);
    for (int i = 0; i < (int)edges.size(); i++) {
        endpointToEdges[contourEndpointKey(edges[i].topA)].push_back(i);
        endpointToEdges[contourEndpointKey(edges[i].topB)].push_back(i);
    }

    int chains = 0;
    std::vector<std::uint8_t> visited(edges.size(), 0);
    std::vector<int> stack;
    for (int i = 0; i < (int)edges.size(); i++) {
        if (visited[i]) {
            continue;
        }

        chains++;
        visited[i] = 1;
        stack.clear();
        stack.push_back(i);
        while (!stack.empty()) {
            const int current = stack.back();
            stack.pop_back();
            const std::array<std::int64_t, 2> keys{
                contourEndpointKey(edges[current].topA),
                contourEndpointKey(edges[current].topB),
            };

            for (std::int64_t key : keys) {
                for (int next : endpointToEdges[key]) {
                    if (!visited[next]) {
                        visited[next] = 1;
                        stack.push_back(next);
                    }
                }
            }
        }
    }
    return chains;
}

void addContourPlateauTop(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    std::vector<Landscape3dRenderer::TerrainVertex>& lineVertices,
    int x,
    int z,
    int grid,
    const TerrainScene& scene,
    const Landscape3dRenderParams& params,
    float topHeight) {

    const std::array<glm::vec3, 4> points = cellDiamondPoints(x, z, grid, params, topHeight);
    const glm::vec4 topColor = materialTint(scene.materialAt(x, z)) * glm::vec4{1.05f, 1.06f, 0.98f, 1.0f};
    const std::uint32_t topBase = (std::uint32_t)vertices.size();
    pushQuadOriented(vertices, indices, points[0], points[1], points[3], points[2], {0.0f, 1.0f, 0.0f}, topColor, FaceKind::Top);
    const float uvScale = 1.0f / std::max(0.1f, params.cubeSize);
    vertices[topBase + 0].uv = {points[0].x * uvScale, points[0].z * uvScale};
    vertices[topBase + 1].uv = {points[1].x * uvScale, points[1].z * uvScale};
    vertices[topBase + 2].uv = {points[3].x * uvScale, points[3].z * uvScale};
    vertices[topBase + 3].uv = {points[2].x * uvScale, points[2].z * uvScale};

    if (params.showWireframe) {
        pushTileWireframe(lineVertices, {points[0], points[1], points[2], points[3], (points[0] + points[1] + points[2] + points[3]) * 0.25f});
    }
}

void pushCliffBandSegment(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const CliffBoundaryEdge& edge,
    int colorLevel,
    int maxLevel,
    bool sideGradient,
    float topHeight) {

    const float edgeLength = glm::length(edge.topB - edge.topA);
    const float relief = std::max(0.025f, edgeLength * 0.14f);
    const float lipWidth = std::max(0.015f, edgeLength * 0.075f);
    const glm::vec4 lipColor = earthColorForLevel(colorLevel + 2, maxLevel + 2);
    glm::vec3 lipA = edge.topA - edge.normal * lipWidth;
    glm::vec3 lipB = edge.topB - edge.normal * lipWidth;
    glm::vec3 lipTopA = edge.topA;
    glm::vec3 lipTopB = edge.topB;
    lipA.y += 0.003f;
    lipB.y += 0.003f;
    lipTopA.y += 0.003f;
    lipTopB.y += 0.003f;
    pushQuadOriented(vertices, indices, lipA, lipB, lipTopA, lipTopB, edge.normal, lipColor, FaceKind::Side);

    auto contourPoint = [&](const glm::vec3& bottom, const glm::vec3& top, float heightT, float edgeT) {
        const float edgeFade = std::sin(edgeT * kPi);
        const float verticalShape = 0.25f + 0.75f * (1.0f - heightT);
        const float curvedOffset = relief * edgeFade * verticalShape;
        return bottom + (top - bottom) * heightT + edge.normal * curvedOffset;
    };

    const std::array<float, 4> bands{0.0f, 0.34f, 0.72f, 1.0f};
    for (int i = 0; i < 3; i++) {
        glm::vec3 a = contourPoint(edge.bottomA, edge.topA, bands[i], 0.0f);
        glm::vec3 b = contourPoint(edge.bottomB, edge.topB, bands[i], 1.0f);
        glm::vec3 c = contourPoint(edge.bottomA, edge.topA, bands[i + 1], 0.0f);
        glm::vec3 d = contourPoint(edge.bottomB, edge.topB, bands[i + 1], 1.0f);
        const glm::vec4 sideColor = earthColorForLevel(colorLevel + i, maxLevel + 2);
        const std::uint32_t base = (std::uint32_t)vertices.size();
        pushQuadOriented(vertices, indices, a, b, c, d, edge.normal, sideColor, FaceKind::Side);
        vertices[base + 0].color = earthVertexColor(colorLevel, maxLevel, a, topHeight, sideGradient);
        vertices[base + 1].color = earthVertexColor(colorLevel, maxLevel, b, topHeight, sideGradient);
        vertices[base + 2].color = earthVertexColor(colorLevel, maxLevel, c, topHeight, sideGradient);
        vertices[base + 3].color = earthVertexColor(colorLevel, maxLevel, d, topHeight, sideGradient);
    }
}

void addContourCliffBands(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    std::vector<Landscape3dRenderer::TerrainVertex>& lineVertices,
    Landscape3dTileStats& stats,
    const TerrainScene& scene,
    const Landscape3dRenderParams& params,
    int grid,
    float heightStep) {

    std::vector<CliffBoundaryEdge> edges;
    edges.reserve((std::size_t)grid * (std::size_t)grid);
    for (int z = 0; z < grid; z++) {
        for (int x = 0; x < grid; x++) {
            std::array<glm::vec3, 5> topPoints{};
            if (!contourTopPointsForCell(scene, params, grid, x, z, heightStep, topPoints)) {
                continue;
            }

            for (int edgeIndex = 0; edgeIndex < 4; edgeIndex++) {
                const glm::ivec2 neighbour = neighbourCellForEdge(x, z, edgeIndex);
                const int next = (edgeIndex + 1) % 4;
                glm::vec3 bottomA = topPoints[edgeIndex];
                glm::vec3 bottomB = topPoints[next];

                std::array<glm::vec3, 5> neighbourPoints{};
                if (contourTopPointsForCell(scene, params, grid, neighbour.x, neighbour.y, heightStep, neighbourPoints)) {
                    const int opposite = (edgeIndex + 2) % 4;
                    bottomA = neighbourPoints[(opposite + 1) % 4];
                    bottomB = neighbourPoints[opposite];
                    if (topPoints[edgeIndex].y + topPoints[next].y <= bottomA.y + bottomB.y + 0.001f) {
                        continue;
                    }
                    bottomA.y = std::min(bottomA.y, topPoints[edgeIndex].y);
                    bottomB.y = std::min(bottomB.y, topPoints[next].y);
                } else {
                    bottomA.y = 0.0f;
                    bottomB.y = 0.0f;
                }

                if (topPoints[edgeIndex].y - bottomA.y <= 0.001f && topPoints[next].y - bottomB.y <= 0.001f) {
                    continue;
                }

                const glm::vec3 center = topPoints[4];
                edges.push_back({
                    topPoints[edgeIndex],
                    topPoints[next],
                    bottomA,
                    bottomB,
                    horizontalNormalFromCenter(center, topPoints[edgeIndex], topPoints[next]),
                });
            }
        }
    }

    stats.contourCliffEdges = (int)edges.size();
    stats.contourCliffChains = countContourChains(edges);

    for (const CliffBoundaryEdge& edge : edges) {
        const int colorLevel = std::max(1, (int)std::lround(edge.topA.y / std::max(0.01f, heightStep)));
        pushCliffBandSegment(vertices, indices, edge, colorLevel, std::max(1, scene.maxHeight()), params.sideGradient, edge.topA.y);
        if (params.showWireframe || params.showEdgeAccents) {
            pushLine(lineVertices, edge.topA, edge.topB);
            pushLine(lineVertices, edge.bottomA, edge.bottomB);
        }
    }
}

void addHighPlateauTile(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    std::vector<Landscape3dRenderer::TerrainVertex>& lineVertices,
    int x,
    int z,
    int grid,
    const TerrainScene& scene,
    const Landscape3dRenderParams& params,
    LandscapeTileType highType,
    float topHeight) {

    if (!tileTypeHasSurface(highType)) return;

    topology_core::StaggeredIsometry iso;
    iso.dims.cellWidth = std::max(0.1f, params.cubeSize);
    iso.dims.aspectRatio = Landscape3dCamera::editorGroundAspectRatio;

    const float cellWidth = iso.dims.cellSize().x;
    const float cellDepth = iso.dims.cellSize().y;
    const glm::vec2 origin = iso.mapToField({grid / 2, grid / 2});
    const glm::vec2 center2 = iso.mapToField({x, z}) - origin;
    const std::array<glm::vec3, 4> points{
        glm::vec3{center2.x - cellWidth * 0.5f, topHeight, center2.y},
        glm::vec3{center2.x, topHeight, center2.y - cellDepth * 0.5f},
        glm::vec3{center2.x + cellWidth * 0.5f, topHeight, center2.y},
        glm::vec3{center2.x, topHeight, center2.y + cellDepth * 0.5f},
    };

    const glm::vec4 topColor = variedGrassTint(scene.materialAt(x, z), x, z, params.grassVariation, 0.45f);
    const std::uint32_t topBase = (std::uint32_t)vertices.size();
    pushQuadOriented(vertices, indices, points[0], points[1], points[3], points[2], {0.0f, 1.0f, 0.0f}, topColor, FaceKind::Top);
    vertices[topBase + 0].uv = variedUv({0.0f, 0.0f}, x, z, params.grassVariation);
    vertices[topBase + 1].uv = variedUv({1.0f, 0.0f}, x, z, params.grassVariation);
    vertices[topBase + 2].uv = variedUv({0.0f, 1.0f}, x, z, params.grassVariation);
    vertices[topBase + 3].uv = variedUv({1.0f, 1.0f}, x, z, params.grassVariation);
    const std::array<glm::vec3, 5> wirePoints{points[0], points[1], points[2], points[3], (points[0] + points[1] + points[2] + points[3]) * 0.25f};
    if (params.showWireframe) {
        pushTileWireframe(lineVertices, wirePoints);
    }

    const glm::vec4 sideColor = earthColorForLevel(scene.columnHeightAt(x, z) + 1, std::max(1, scene.maxHeight()));
    for (int edge = 0; edge < 4; edge++) {
        const glm::ivec2 neighbour = neighbourCellForEdge(x, z, edge);
        const bool neighbourHigh = neighbour.x >= 0 &&
            neighbour.y >= 0 &&
            neighbour.x < grid &&
            neighbour.y < grid &&
            tileTypeHasSurface(scene.tileTypeAtLevel(neighbour.x, neighbour.y, 2));
        if (neighbourHigh) {
            continue;
        }

        const int next = (edge + 1) % 4;
        glm::vec3 baseA = points[edge];
        glm::vec3 baseB = points[next];
        baseA.y = 0.0f;
        baseB.y = 0.0f;
        const glm::vec3 center{center2.x, topHeight, center2.y};
        const glm::vec3 normal = horizontalNormalFromCenter(center, points[edge], points[next]);
        const std::uint32_t base = (std::uint32_t)vertices.size();
        pushQuadOriented(vertices, indices, baseA, baseB, points[edge], points[next], normal, sideColor, FaceKind::Side);
        vertices[base + 0].color = earthVertexColor(scene.columnHeightAt(x, z) + 1, std::max(1, scene.maxHeight()), baseA, topHeight, params.sideGradient);
        vertices[base + 1].color = earthVertexColor(scene.columnHeightAt(x, z) + 1, std::max(1, scene.maxHeight()), baseB, topHeight, params.sideGradient);
        vertices[base + 2].color = earthVertexColor(scene.columnHeightAt(x, z) + 1, std::max(1, scene.maxHeight()), points[edge], topHeight, params.sideGradient);
        vertices[base + 3].color = earthVertexColor(scene.columnHeightAt(x, z) + 1, std::max(1, scene.maxHeight()), points[next], topHeight, params.sideGradient);
        if (params.showWireframe || params.showEdgeAccents) {
            pushLine(lineVertices, points[edge], points[next]);
            pushLine(lineVertices, baseA, baseB);
        }
    }
}

const char* vs_src_glsl = R"(
#version 330
layout(location=0) in vec3 pos;
layout(location=1) in vec3 normal0;
layout(location=2) in vec4 color0;
layout(location=3) in vec2 uv0;
layout(location=4) in float faceKind0;
out vec3 v_normal;
out vec4 v_color;
out vec2 v_uv;
out float v_faceKind;
out float v_height;
uniform mat4 mvp;
void main() {
    gl_Position = mvp * vec4(pos, 1.0);
    v_normal = normal0;
    v_color = color0;
    v_uv = uv0;
    v_faceKind = faceKind0;
    v_height = pos.y;
}
)";

const char* fs_src_glsl = R"(
#version 330
in vec3 v_normal;
in vec4 v_color;
in vec2 v_uv;
in float v_faceKind;
in float v_height;
out vec4 frag_color;
uniform sampler2D grass_tex;
uniform sampler2D rock_tex;
uniform vec4 light_dir;
uniform vec4 options; // x debug, y useTerrainTextures, z AO strength
void main() {
    int debugMode = int(options.x + 0.5);
    bool useTerrainTextures = options.y > 0.5;
    bool isTop = v_faceKind < 0.5;
    bool isSide = v_faceKind > 0.5 && v_faceKind < 1.5;
    vec3 n = normalize(v_normal);

    vec4 base = v_color;
    if (isTop && useTerrainTextures) {
        base = texture(grass_tex, v_uv) * v_color;
    } else if (isSide && useTerrainTextures) {
        vec4 rock = texture(rock_tex, v_uv);
        base = vec4(rock.rgb, rock.a * v_color.a);
    }

    if (debugMode == 1) {
        frag_color = isTop ? base : vec4(0.08, 0.08, 0.08, 1.0);
        return;
    }
    if (debugMode == 2) {
        frag_color = isTop ? vec4(0.08, 0.08, 0.08, 1.0) : base;
        return;
    }
    if (debugMode == 3) {
        float h = clamp(v_height * 0.08, 0.0, 1.0);
        frag_color = vec4(h, h * 0.75, 1.0 - h, 1.0);
        return;
    }
    if (debugMode == 4) {
        frag_color = vec4(n * 0.5 + 0.5, 1.0);
        return;
    }

    float diffuse = max(dot(n, normalize(light_dir.xyz)), 0.0);
    vec3 warmAmbient = vec3(0.42, 0.36, 0.27);
    vec3 coolShadow = vec3(0.14, 0.20, 0.22);
    vec3 lightTint = vec3(1.05, 0.96, 0.82);
    float sideMask = isTop ? 0.0 : 1.0;
    float groundContact = sideMask * (1.0 - smoothstep(0.02, 0.48, v_height));
    float sideOcclusion = sideMask * (1.0 - abs(n.y));
    float ao = clamp((groundContact * 0.85 + sideOcclusion * 0.34) * options.z, 0.0, 0.88);
    vec3 lit = base.rgb * (mix(coolShadow, warmAmbient, 0.62) + diffuse * 0.78 * lightTint);
    lit *= 1.0 - ao;
    frag_color = vec4(lit, base.a);
}
)";

const char* vs_src_hlsl = R"(
cbuffer vs_params: register(b0) { float4x4 mvp; };
struct VSIn {
    float3 pos: TEXCOORD0;
    float3 normal0: TEXCOORD1;
    float4 color0: TEXCOORD2;
    float2 uv0: TEXCOORD3;
    float faceKind0: TEXCOORD4;
};
struct VSOut {
    float4 pos: SV_Position;
    float3 normal0: TEXCOORD0;
    float4 color0: TEXCOORD1;
    float2 uv0: TEXCOORD2;
    float faceKind0: TEXCOORD3;
    float height0: TEXCOORD4;
};
VSOut main(VSIn inp) {
    VSOut o;
    o.pos = mul(mvp, float4(inp.pos, 1.0));
    o.normal0 = inp.normal0;
    o.color0 = inp.color0;
    o.uv0 = inp.uv0;
    o.faceKind0 = inp.faceKind0;
    o.height0 = inp.pos.y;
    return o;
}
)";

const char* fs_src_hlsl = R"(
Texture2D grass_tex: register(t0);
Texture2D rock_tex: register(t1);
SamplerState grass_smp: register(s0);
SamplerState rock_smp: register(s1);
cbuffer fs_params: register(b0) { float4 light_dir; float4 options; };
struct PSIn {
    float4 pos: SV_Position;
    float3 normal0: TEXCOORD0;
    float4 color0: TEXCOORD1;
    float2 uv0: TEXCOORD2;
    float faceKind0: TEXCOORD3;
    float height0: TEXCOORD4;
};
float4 main(PSIn inp): SV_Target0 {
    int debugMode = (int)(options.x + 0.5);
    bool useTerrainTextures = options.y > 0.5;
    bool isTop = inp.faceKind0 < 0.5;
    bool isSide = inp.faceKind0 > 0.5 && inp.faceKind0 < 1.5;
    float3 n = normalize(inp.normal0);

    float4 base = inp.color0;
    if (isTop && useTerrainTextures) {
        base = grass_tex.Sample(grass_smp, inp.uv0) * inp.color0;
    } else if (isSide && useTerrainTextures) {
        float4 rock = rock_tex.Sample(rock_smp, inp.uv0);
        base = float4(rock.rgb, rock.a * inp.color0.a);
    }

    if (debugMode == 1) {
        return isTop ? base : float4(0.08, 0.08, 0.08, 1.0);
    }
    if (debugMode == 2) {
        return isTop ? float4(0.08, 0.08, 0.08, 1.0) : base;
    }
    if (debugMode == 3) {
        float h = saturate(inp.height0 * 0.08);
        return float4(h, h * 0.75, 1.0 - h, 1.0);
    }
    if (debugMode == 4) {
        return float4(n * 0.5 + 0.5, 1.0);
    }

    float diffuse = max(dot(n, normalize(light_dir.xyz)), 0.0);
    float3 warmAmbient = float3(0.42, 0.36, 0.27);
    float3 coolShadow = float3(0.14, 0.20, 0.22);
    float3 lightTint = float3(1.05, 0.96, 0.82);
    float sideMask = isTop ? 0.0 : 1.0;
    float groundContact = sideMask * (1.0 - smoothstep(0.02, 0.48, inp.height0));
    float sideOcclusion = sideMask * (1.0 - abs(n.y));
    float ao = saturate((groundContact * 0.85 + sideOcclusion * 0.34) * options.z);
    ao = min(ao, 0.88);
    float3 lit = base.rgb * (lerp(coolShadow, warmAmbient, 0.62) + diffuse * 0.78 * lightTint);
    lit *= 1.0 - ao;
    return float4(lit, base.a);
}
)";

bool loadTextureFromFile(
    const std::filesystem::path& path,
    const std::uint8_t* fallbackPixels,
    int fallbackWidth,
    int fallbackHeight,
    const char* textureName,
    const char* imageLabel,
    const char* samplerLabel,
    sg_filter filter,
    sg_image& image,
    sg_view& view,
    sg_sampler& sampler) {

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    const bool loadedFromFile = pixels && width > 0 && height > 0;

    if (!loadedFromFile) {
        spdlog::warn("Landscape3dRenderer: failed to load {} texture '{}', using fallback", textureName, path.string());
        if (pixels) {
            stbi_image_free(pixels);
            pixels = nullptr;
        }
        width = fallbackWidth;
        height = fallbackHeight;
    }

    sg_image_desc imgDesc = {};
    imgDesc.width = width;
    imgDesc.height = height;
    imgDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    imgDesc.data.mip_levels[0].ptr = loadedFromFile ? pixels : fallbackPixels;
    imgDesc.data.mip_levels[0].size = (std::size_t)width * (std::size_t)height * 4;
    imgDesc.label = imageLabel;
    image = sg_make_image(&imgDesc);
    if (pixels) {
        stbi_image_free(pixels);
    }

    if (image.id == SG_INVALID_ID) {
        return false;
    }

    sg_view_desc viewDesc = {};
    viewDesc.texture.image = image;
    view = sg_make_view(&viewDesc);

    sg_sampler_desc samplerDesc = {};
    samplerDesc.min_filter = filter;
    samplerDesc.mag_filter = filter;
    samplerDesc.wrap_u = SG_WRAP_REPEAT;
    samplerDesc.wrap_v = SG_WRAP_REPEAT;
    samplerDesc.label = samplerLabel;
    sampler = sg_make_sampler(&samplerDesc);

    return loadedFromFile && view.id != SG_INVALID_ID && sampler.id != SG_INVALID_ID;
}

} // namespace

glm::vec3 Landscape3dCamera::position() const {
    const float yaw = radians(yawDeg);
    const float pitch = radians(pitchDeg);
    const float cp = std::cos(pitch);
    return target + glm::vec3(
        std::sin(yaw) * cp * distance,
        std::sin(pitch) * distance,
        std::cos(yaw) * cp * distance);
}

glm::vec3 Landscape3dCamera::right() const {
    return glm::normalize(glm::cross(glm::normalize(target - position()), glm::vec3(0.0f, 1.0f, 0.0f)));
}

glm::vec3 Landscape3dCamera::up() const {
    const glm::vec3 forward = glm::normalize(target - position());
    return glm::normalize(glm::cross(right(), forward));
}

glm::mat4 Landscape3dCamera::viewMatrix() const {
    return glm::lookAtRH(position(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Landscape3dCamera::projectionMatrix(float aspect) const {
    if (perspective) {
        return glm::perspectiveRH_ZO(radians(38.0f), aspect, 0.1f, 300.0f);
    }

    const float y = std::max(0.35f, orthoScale);
    const float x = y * std::max(0.1f, aspect);
    return glm::orthoRH_ZO(-x, x, -y, y, -200.0f, 300.0f);
}

void Landscape3dRenderer::init() {
    ensurePipelines();
}

void Landscape3dRenderer::shutdown() {
    destroyMeshBuffers();
    destroyRockTexture();
    destroyGrassTexture();
    destroyPipelines();
}

bool Landscape3dRenderer::loadGrassTexture(const std::filesystem::path& path) {
    destroyGrassTexture();
    const std::uint8_t fallbackPixels[] = {
        72, 145, 48, 255,
        58, 120, 39, 255,
        84, 164, 56, 255,
        66, 132, 44, 255,
    };

    return loadTextureFromFile(
        path,
        fallbackPixels,
        2,
        2,
        "grass",
        "landscape3d-grass",
        "landscape3d-grass-sampler",
        SG_FILTER_NEAREST,
        m_grassImage,
        m_grassView,
        m_grassSampler);
}

bool Landscape3dRenderer::loadRockTexture(const std::filesystem::path& path) {
    destroyRockTexture();
    const std::uint8_t fallbackPixels[] = {
        112, 108, 100, 255,
        142, 136, 124, 255,
        86, 82, 76, 255,
        164, 158, 144, 255,
    };

    return loadTextureFromFile(
        path,
        fallbackPixels,
        2,
        2,
        "rock",
        "landscape3d-rock",
        "landscape3d-rock-sampler",
        SG_FILTER_LINEAR,
        m_rockImage,
        m_rockView,
        m_rockSampler);
}

void Landscape3dRenderer::rebuildMesh(const TerrainScene& scene, const Landscape3dRenderParams& params) {
    destroyMeshBuffers();
    m_tileStats = {};

    if (scene.gridSize() <= 0) return;

    std::vector<TerrainVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<TerrainVertex> lineVertices;

    const int grid = scene.gridSize();
    const int maxHeight = std::max(1, scene.maxHeight());
    const float cellSize = std::max(0.1f, params.cubeSize);
    const float heightStep = valleyHeightStep(cellSize, params);
    vertices.reserve((std::size_t)grid * (std::size_t)grid * 24);
    indices.reserve((std::size_t)grid * (std::size_t)grid * 36);
    lineVertices.reserve((std::size_t)grid * (std::size_t)grid * 24);

    auto heightAtOrZero = [&](int x, int z) {
        if (x < 0 || z < 0 || x >= grid || z >= grid) return 0;
        return scene.columnHeightAt(x, z);
    };

    if (params.terrainMode == Landscape3dTerrainMode::CubesDebug) {
        for (int z = 0; z < grid; z++) {
            for (int x = 0; x < grid; x++) {
                const int height = scene.columnHeightAt(x, z);
                if (height <= 0) continue;

                const glm::vec4 topColor = materialTint(scene.materialAt(x, z));

                for (int level = 0; level < height; level++) {
                    const bool isTop = level == (height - 1);
                    const glm::vec4 sideColor = earthColorForLevel(level, maxHeight);

                    const glm::vec3 p000 = cubeCorner(x, level, z, cellSize, grid);
                    const glm::vec3 p100 = cubeCorner(x + 1, level, z, cellSize, grid);
                    const glm::vec3 p010 = cubeCorner(x, level + 1, z, cellSize, grid);
                    const glm::vec3 p110 = cubeCorner(x + 1, level + 1, z, cellSize, grid);
                    const glm::vec3 p001 = cubeCorner(x, level, z + 1, cellSize, grid);
                    const glm::vec3 p101 = cubeCorner(x + 1, level, z + 1, cellSize, grid);
                    const glm::vec3 p011 = cubeCorner(x, level + 1, z + 1, cellSize, grid);
                    const glm::vec3 p111 = cubeCorner(x + 1, level + 1, z + 1, cellSize, grid);

                    if (isTop) {
                        pushQuadOriented(vertices, indices, p010, p110, p011, p111, {0.0f, 1.0f, 0.0f}, topColor, FaceKind::Top);
                    }

                    if (heightAtOrZero(x, z - 1) <= level) {
                        pushQuadOriented(vertices, indices, p000, p100, p010, p110, {0.0f, 0.0f, -1.0f}, sideColor, FaceKind::Side);
                    }
                    if (heightAtOrZero(x + 1, z) <= level) {
                        pushQuadOriented(vertices, indices, p100, p101, p110, p111, {1.0f, 0.0f, 0.0f}, sideColor, FaceKind::Side);
                    }
                    if (heightAtOrZero(x, z + 1) <= level) {
                        pushQuadOriented(vertices, indices, p101, p001, p111, p011, {0.0f, 0.0f, 1.0f}, sideColor, FaceKind::Side);
                    }
                    if (heightAtOrZero(x - 1, z) <= level) {
                        pushQuadOriented(vertices, indices, p001, p000, p011, p010, {-1.0f, 0.0f, 0.0f}, sideColor, FaceKind::Side);
                    }

                    if (params.showWireframe) {
                        pushBoxWireframe(lineVertices, p000, p100, p010, p110, p001, p101, p011, p111);
                    }
                }
            }
        }
    } else if (params.terrainMode == Landscape3dTerrainMode::ValleyGeometry) {
        for (int z = 0; z < grid; z++) {
            for (int x = 0; x < grid; x++) {
                const LandscapeTileType lowType = params.previewTileIndex >= 0
                    ? tileTypeFromAtlasIndex(params.previewTileIndex)
                    : scene.tileTypeAtLevel(x, z, 1);
                const LandscapeTileType highType = params.previewTileIndex >= 0
                    ? LandscapeTileType::Unknown
                    : scene.tileTypeAtLevel(x, z, 2);
                addTileStat(m_tileStats, lowType);
                addZeroLayerFill(vertices, indices, x, z, grid, params);
                if (tileTypeHasSurface(lowType) && !tileTypeHasSurface(highType)) {
                    addValleyTileObject(vertices, indices, lineVertices, x, z, grid, scene, params, lowType, 0.0f, heightStep, true, params.showWireframe);
                }
                if (tileTypeHasSurface(highType)) {
                    addHighPlateauTile(vertices, indices, lineVertices, x, z, grid, scene, params, highType, heightStep * 2.0f);
                }
            }
        }
    } else {
        for (int z = 0; z < grid; z++) {
            for (int x = 0; x < grid; x++) {
                const LandscapeTileType lowType = cellBandType(scene, grid, x, z, 1);
                const LandscapeTileType highType = cellBandType(scene, grid, x, z, 2);
                addTileStat(m_tileStats, lowType);
                addZeroLayerFill(vertices, indices, x, z, grid, params);

                if (tileTypeHasSurface(highType)) {
                    m_tileStats.contourHighCells++;
                    addValleyTileObject(
                        vertices,
                        indices,
                        lineVertices,
                        x,
                        z,
                        grid,
                        scene,
                        params,
                        highType,
                        bandBaseHeight(2, heightStep),
                        heightStep,
                        false,
                        params.showWireframe,
                        nullptr);
                } else if (tileTypeHasSurface(lowType)) {
                    addValleyTileObject(
                        vertices,
                        indices,
                        lineVertices,
                        x,
                        z,
                        grid,
                        scene,
                        params,
                        lowType,
                        bandBaseHeight(1, heightStep),
                        heightStep,
                        false,
                        params.showWireframe,
                        nullptr);
                }
            }
        }

        addContourCliffBands(vertices, indices, lineVertices, m_tileStats, scene, params, grid, heightStep);
    }

    if (vertices.empty() || indices.empty()) {
        return;
    }

    sg_buffer_desc vbuf = {};
    vbuf.usage.vertex_buffer = true;
    vbuf.data.ptr = vertices.data();
    vbuf.data.size = vertices.size() * sizeof(TerrainVertex);
    vbuf.label = "landscape3d-cube-vertices";
    m_vertexBuffer = sg_make_buffer(&vbuf);

    sg_buffer_desc ibuf = {};
    ibuf.usage.index_buffer = true;
    ibuf.data.ptr = indices.data();
    ibuf.data.size = indices.size() * sizeof(std::uint32_t);
    ibuf.label = "landscape3d-cube-indices";
    m_indexBuffer = sg_make_buffer(&ibuf);

    if (!lineVertices.empty()) {
        sg_buffer_desc lbuf = {};
        lbuf.usage.vertex_buffer = true;
        lbuf.data.ptr = lineVertices.data();
        lbuf.data.size = lineVertices.size() * sizeof(TerrainVertex);
        lbuf.label = "landscape3d-cube-lines";
        m_lineVertexBuffer = sg_make_buffer(&lbuf);
    }

    m_terrainBindings = {};
    m_terrainBindings.vertex_buffers[0] = m_vertexBuffer;
    m_terrainBindings.index_buffer = m_indexBuffer;
    if (m_grassView.id != SG_INVALID_ID && m_grassSampler.id != SG_INVALID_ID) {
        m_terrainBindings.views[0] = m_grassView;
        m_terrainBindings.samplers[0] = m_grassSampler;
    }
    if (m_rockView.id != SG_INVALID_ID && m_rockSampler.id != SG_INVALID_ID) {
        m_terrainBindings.views[1] = m_rockView;
        m_terrainBindings.samplers[1] = m_rockSampler;
    }
    m_lineBindings = {};
    if (m_lineVertexBuffer.id != SG_INVALID_ID) {
        m_lineBindings.vertex_buffers[0] = m_lineVertexBuffer;
    }
    if (m_grassView.id != SG_INVALID_ID && m_grassSampler.id != SG_INVALID_ID) {
        m_lineBindings.views[0] = m_grassView;
        m_lineBindings.samplers[0] = m_grassSampler;
    }
    if (m_rockView.id != SG_INVALID_ID && m_rockSampler.id != SG_INVALID_ID) {
        m_lineBindings.views[1] = m_rockView;
        m_lineBindings.samplers[1] = m_rockSampler;
    }
    m_indexCount = (int)indices.size();
    m_lineVertexCount = (int)lineVertices.size();
}

void Landscape3dRenderer::render(const Landscape3dCamera& camera, const Landscape3dRenderParams& params, int width, int height) {
    if (m_terrainPipeline.id == SG_INVALID_ID || m_vertexBuffer.id == SG_INVALID_ID || m_indexCount <= 0) return;

    const float aspect = (height > 0) ? ((float)width / (float)height) : 1.0f;
    const glm::mat4 mvp = camera.projectionMatrix(aspect) * camera.viewMatrix();

    VsParams vs = {};
    std::memcpy(vs.mvp, glm::value_ptr(mvp), sizeof(vs.mvp));

    const float yaw = radians(params.lightYawDeg);
    const float pitch = radians(params.lightPitchDeg);
    const glm::vec3 lightDir = glm::normalize(glm::vec3(
        std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::cos(yaw) * std::cos(pitch)));

    FsParams fs = {};
    fs.lightDir[0] = lightDir.x;
    fs.lightDir[1] = lightDir.y;
    fs.lightDir[2] = lightDir.z;
    fs.lightDir[3] = 0.0f;
    fs.options[0] = (float)params.debugMode;
    fs.options[1] = params.useGrassTexture ? 1.0f : 0.0f;
    fs.options[2] = std::clamp(params.ambientOcclusionStrength, 0.0f, 1.0f);

    sg_range vsRange = {&vs, sizeof(vs)};
    sg_range fsRange = {&fs, sizeof(fs)};

    sg_apply_pipeline(m_terrainPipeline);
    sg_apply_bindings(&m_terrainBindings);
    sg_apply_uniforms(0, &vsRange);
    sg_apply_uniforms(1, &fsRange);
    sg_draw(0, m_indexCount, 1);

    if ((params.showWireframe || params.showEdgeAccents) && m_linePipeline.id != SG_INVALID_ID && m_lineVertexCount > 0) {
        FsParams lineFs = fs;
        lineFs.options[0] = 2.0f;
        sg_range lineFsRange = {&lineFs, sizeof(lineFs)};

        sg_apply_pipeline(m_linePipeline);
        sg_apply_bindings(&m_lineBindings);
        sg_apply_uniforms(0, &vsRange);
        sg_apply_uniforms(1, &lineFsRange);
        sg_draw(0, m_lineVertexCount, 1);
    }
}

void Landscape3dRenderer::ensurePipelines() {
    if (m_terrainPipeline.id != SG_INVALID_ID) return;

    sg_shader_desc shd = {};
#if defined(SOKOL_D3D11)
    shd.vertex_func.source = vs_src_hlsl;
    shd.fragment_func.source = fs_src_hlsl;
    shd.attrs[0].hlsl_sem_name = "TEXCOORD";
    shd.attrs[0].hlsl_sem_index = 0;
    shd.attrs[1].hlsl_sem_name = "TEXCOORD";
    shd.attrs[1].hlsl_sem_index = 1;
    shd.attrs[2].hlsl_sem_name = "TEXCOORD";
    shd.attrs[2].hlsl_sem_index = 2;
    shd.attrs[3].hlsl_sem_name = "TEXCOORD";
    shd.attrs[3].hlsl_sem_index = 3;
    shd.attrs[4].hlsl_sem_name = "TEXCOORD";
    shd.attrs[4].hlsl_sem_index = 4;
#else
    shd.vertex_func.source = vs_src_glsl;
    shd.fragment_func.source = fs_src_glsl;
#endif

    shd.uniform_blocks[0].stage = SG_SHADERSTAGE_VERTEX;
    shd.uniform_blocks[0].size = sizeof(VsParams);
    shd.uniform_blocks[0].hlsl_register_b_n = 0;
    shd.uniform_blocks[0].msl_buffer_n = 0;
    shd.uniform_blocks[0].wgsl_group0_binding_n = 0;
    shd.uniform_blocks[0].spirv_set0_binding_n = 0;
    shd.uniform_blocks[0].glsl_uniforms[0].glsl_name = "mvp";
    shd.uniform_blocks[0].glsl_uniforms[0].type = SG_UNIFORMTYPE_MAT4;

    shd.uniform_blocks[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.uniform_blocks[1].size = sizeof(FsParams);
    shd.uniform_blocks[1].hlsl_register_b_n = 0;
    shd.uniform_blocks[1].msl_buffer_n = 1;
    shd.uniform_blocks[1].wgsl_group0_binding_n = 1;
    shd.uniform_blocks[1].spirv_set0_binding_n = 1;
    shd.uniform_blocks[1].glsl_uniforms[0].glsl_name = "light_dir";
    shd.uniform_blocks[1].glsl_uniforms[0].type = SG_UNIFORMTYPE_FLOAT4;
    shd.uniform_blocks[1].glsl_uniforms[1].glsl_name = "options";
    shd.uniform_blocks[1].glsl_uniforms[1].type = SG_UNIFORMTYPE_FLOAT4;

    shd.views[0].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd.views[0].texture.image_type = SG_IMAGETYPE_2D;
    shd.views[0].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd.views[0].texture.hlsl_register_t_n = 0;
    shd.views[0].texture.msl_texture_n = 0;
    shd.views[0].texture.wgsl_group1_binding_n = 0;
    shd.views[0].texture.spirv_set1_binding_n = 0;
    shd.samplers[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.samplers[0].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd.samplers[0].hlsl_register_s_n = 0;
    shd.samplers[0].msl_sampler_n = 0;
    shd.samplers[0].wgsl_group1_binding_n = 1;
    shd.samplers[0].spirv_set1_binding_n = 1;
    shd.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.texture_sampler_pairs[0].view_slot = 0;
    shd.texture_sampler_pairs[0].sampler_slot = 0;
    shd.texture_sampler_pairs[0].glsl_name = "grass_tex";
    shd.views[1].texture.stage = SG_SHADERSTAGE_FRAGMENT;
    shd.views[1].texture.image_type = SG_IMAGETYPE_2D;
    shd.views[1].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    shd.views[1].texture.hlsl_register_t_n = 1;
    shd.views[1].texture.msl_texture_n = 1;
    shd.views[1].texture.wgsl_group1_binding_n = 2;
    shd.views[1].texture.spirv_set1_binding_n = 2;
    shd.samplers[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.samplers[1].sampler_type = SG_SAMPLERTYPE_FILTERING;
    shd.samplers[1].hlsl_register_s_n = 1;
    shd.samplers[1].msl_sampler_n = 1;
    shd.samplers[1].wgsl_group1_binding_n = 3;
    shd.samplers[1].spirv_set1_binding_n = 3;
    shd.texture_sampler_pairs[1].stage = SG_SHADERSTAGE_FRAGMENT;
    shd.texture_sampler_pairs[1].view_slot = 1;
    shd.texture_sampler_pairs[1].sampler_slot = 1;
    shd.texture_sampler_pairs[1].glsl_name = "rock_tex";

    shd.label = "landscape3d-cube-shader";
    m_shader = sg_make_shader(&shd);

    sg_pipeline_desc terrain = {};
    terrain.shader = m_shader;
    terrain.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    terrain.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT3;
    terrain.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4;
    terrain.layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT2;
    terrain.layout.attrs[4].format = SG_VERTEXFORMAT_FLOAT;
    terrain.index_type = SG_INDEXTYPE_UINT32;
    terrain.primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    terrain.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    terrain.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
    terrain.depth.write_enabled = true;
    terrain.label = "landscape3d-cube-pipeline";
    m_terrainPipeline = sg_make_pipeline(&terrain);

    sg_pipeline_desc lines = terrain;
    lines.index_type = SG_INDEXTYPE_NONE;
    lines.primitive_type = SG_PRIMITIVETYPE_LINES;
    lines.depth.write_enabled = false;
    lines.label = "landscape3d-cube-line-pipeline";
    m_linePipeline = sg_make_pipeline(&lines);
}

void Landscape3dRenderer::destroyPipelines() {
    if (m_linePipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_linePipeline);
        m_linePipeline.id = SG_INVALID_ID;
    }
    if (m_terrainPipeline.id != SG_INVALID_ID) {
        sg_destroy_pipeline(m_terrainPipeline);
        m_terrainPipeline.id = SG_INVALID_ID;
    }
    if (m_shader.id != SG_INVALID_ID) {
        sg_destroy_shader(m_shader);
        m_shader.id = SG_INVALID_ID;
    }
}

void Landscape3dRenderer::destroyMeshBuffers() {
    if (m_lineVertexBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_lineVertexBuffer);
        m_lineVertexBuffer.id = SG_INVALID_ID;
    }
    if (m_indexBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_indexBuffer);
        m_indexBuffer.id = SG_INVALID_ID;
    }
    if (m_vertexBuffer.id != SG_INVALID_ID) {
        sg_destroy_buffer(m_vertexBuffer);
        m_vertexBuffer.id = SG_INVALID_ID;
    }

    m_terrainBindings = {};
    m_lineBindings = {};
    m_indexCount = 0;
    m_lineVertexCount = 0;
}

void Landscape3dRenderer::destroyGrassTexture() {
    if (m_grassSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_grassSampler);
        m_grassSampler.id = SG_INVALID_ID;
    }
    if (m_grassView.id != SG_INVALID_ID) {
        sg_destroy_view(m_grassView);
        m_grassView.id = SG_INVALID_ID;
    }
    if (m_grassImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_grassImage);
        m_grassImage.id = SG_INVALID_ID;
    }
}

void Landscape3dRenderer::destroyRockTexture() {
    if (m_rockSampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(m_rockSampler);
        m_rockSampler.id = SG_INVALID_ID;
    }
    if (m_rockView.id != SG_INVALID_ID) {
        sg_destroy_view(m_rockView);
        m_rockView.id = SG_INVALID_ID;
    }
    if (m_rockImage.id != SG_INVALID_ID) {
        sg_destroy_image(m_rockImage);
        m_rockImage.id = SG_INVALID_ID;
    }
}

