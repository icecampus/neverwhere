#include "Landscape3dRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <stdexcept>

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

float radians(float deg) {
    return deg * kPi / 180.0f;
}

glm::vec4 earthColorForLevel(int level, int maxLevel) {
    const float t = (maxLevel > 0) ? ((float)level / (float)maxLevel) : 0.0f;
    return {
        0.36f + 0.07f * t,
        0.22f + 0.05f * t,
        0.12f + 0.03f * t,
        1.0f
    };
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

glm::vec3 cubeCorner(int x, int y, int z, float cubeSize, int gridSize) {
    const float half = (float)gridSize * cubeSize * 0.5f;
    return {
        (float)x * cubeSize - half,
        (float)y * cubeSize,
        (float)z * cubeSize - half
    };
}

void pushQuad(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec3& d,
    const glm::vec3& normal,
    const glm::vec4& color,
    FaceKind faceKind,
    bool flip = false) {

    const std::uint32_t base = (std::uint32_t)vertices.size();
    const float kind = (float)(int)faceKind;
    vertices.push_back({a, normal, color, {0.0f, 0.0f}, kind});
    vertices.push_back({b, normal, color, {1.0f, 0.0f}, kind});
    vertices.push_back({c, normal, color, {0.0f, 1.0f}, kind});
    vertices.push_back({d, normal, color, {1.0f, 1.0f}, kind});

    if (!flip) {
        indices.push_back(base + 0);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 3);
    } else {
        indices.push_back(base + 0);
        indices.push_back(base + 1);
        indices.push_back(base + 2);
        indices.push_back(base + 1);
        indices.push_back(base + 3);
        indices.push_back(base + 2);
    }
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
    const glm::vec4 lineColor{0.035f, 0.035f, 0.03f, 1.0f};
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

void addValleyTileObject(
    std::vector<Landscape3dRenderer::TerrainVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    std::vector<Landscape3dRenderer::TerrainVertex>& lineVertices,
    int x,
    int z,
    int grid,
    const TerrainScene& scene,
    const Landscape3dRenderParams& params,
    LandscapeTileType type) {

    const LandscapeValleyGeometry geometry = valleyGeometryForTile(type);
    if (geometry.triangleCount <= 0) return;

    topology_core::StaggeredIsometry iso;
    iso.dims.cellWidth = std::max(0.1f, params.cubeSize);
    iso.dims.aspectRatio = 2.0f;

    const float cellWidth = iso.dims.cellSize().x;
    const float cellDepth = iso.dims.cellSize().y;
    const glm::vec2 origin = iso.mapToField({grid / 2, grid / 2});
    const glm::vec2 center2 = iso.mapToField({x, z}) - origin;
    const float heightScale = std::max(0.01f, params.tileHeight) * cellWidth;

    const std::array<glm::vec2, 5> offsets{
        glm::vec2{-cellWidth * 0.5f, 0.0f},
        glm::vec2{0.0f, -cellDepth * 0.5f},
        glm::vec2{cellWidth * 0.5f, 0.0f},
        glm::vec2{0.0f, cellDepth * 0.5f},
        glm::vec2{0.0f, 0.0f},
    };
    const std::array<glm::vec2, 5> uvs{
        glm::vec2{0.0f, 0.5f},
        glm::vec2{0.5f, 0.0f},
        glm::vec2{1.0f, 0.5f},
        glm::vec2{0.5f, 1.0f},
        glm::vec2{0.5f, 0.5f},
    };

    std::array<glm::vec3, 5> points{};
    for (int i = 0; i < 5; i++) {
        const glm::vec2 p = center2 + offsets[i];
        points[i] = {p.x, geometry.heights[i] * heightScale, p.y};
    }

    const glm::vec4 topColor = tileDebugColor(type, scene.materialAt(x, z));
    for (int i = 0; i < geometry.triangleCount; i++) {
        const std::array<std::uint8_t, 3>& tri = geometry.triangles[i];
        pushTriangle(
            vertices,
            indices,
            points[tri[0]],
            points[tri[1]],
            points[tri[2]],
            uvs[tri[0]],
            uvs[tri[1]],
            uvs[tri[2]],
            topColor,
            FaceKind::Top);
    }

    const glm::vec4 sideColor = earthColorForLevel(scene.columnHeightAt(x, z), std::max(1, scene.maxHeight()));
    for (int i = 0; i < 4; i++) {
        const int next = (i + 1) % 4;
        if (geometry.heights[i] <= 0.0f && geometry.heights[next] <= 0.0f) {
            continue;
        }

        glm::vec3 baseA = points[i];
        glm::vec3 baseB = points[next];
        baseA.y = 0.0f;
        baseB.y = 0.0f;
        const glm::vec3 normal = horizontalNormalFromCenter(points[4], points[i], points[next]);
        pushQuad(vertices, indices, baseA, baseB, points[i], points[next], normal, sideColor, FaceKind::Side, true);
    }

    pushTileWireframe(lineVertices, points);
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
uniform vec4 light_dir;
uniform vec4 options; // x debug, y useGrassTexture
void main() {
    int debugMode = int(options.x + 0.5);
    bool useGrassTexture = options.y > 0.5;
    bool isTop = v_faceKind < 0.5;
    vec3 n = normalize(v_normal);

    vec4 base = v_color;
    if (isTop && useGrassTexture) {
        base = texture(grass_tex, v_uv) * v_color;
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
    vec3 lit = base.rgb * (0.34 + diffuse * 0.66);
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
SamplerState grass_smp: register(s0);
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
    bool useGrassTexture = options.y > 0.5;
    bool isTop = inp.faceKind0 < 0.5;
    float3 n = normalize(inp.normal0);

    float4 base = inp.color0;
    if (isTop && useGrassTexture) {
        base = grass_tex.Sample(grass_smp, inp.uv0) * inp.color0;
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
    float3 lit = base.rgb * (0.34 + diffuse * 0.66);
    return float4(lit, base.a);
}
)";

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

    const float y = std::max(1.0f, orthoScale);
    const float x = y * std::max(0.1f, aspect);
    return glm::orthoRH_ZO(-x, x, -y, y, -200.0f, 300.0f);
}

void Landscape3dRenderer::init() {
    ensurePipelines();
}

void Landscape3dRenderer::shutdown() {
    destroyMeshBuffers();
    destroyGrassTexture();
    destroyPipelines();
}

bool Landscape3dRenderer::loadGrassTexture(const std::filesystem::path& path) {
    destroyGrassTexture();

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    const bool loadedFromFile = pixels && width > 0 && height > 0;
    const std::uint8_t fallbackPixels[] = {
        72, 145, 48, 255,
        58, 120, 39, 255,
        84, 164, 56, 255,
        66, 132, 44, 255,
    };

    if (!loadedFromFile) {
        spdlog::warn("Landscape3dRenderer: failed to load grass texture '{}', using fallback", path.string());
        if (pixels) {
            stbi_image_free(pixels);
            pixels = nullptr;
        }
        width = 2;
        height = 2;
    }

    sg_image_desc imgDesc = {};
    imgDesc.width = width;
    imgDesc.height = height;
    imgDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    imgDesc.data.mip_levels[0].ptr = loadedFromFile ? pixels : fallbackPixels;
    imgDesc.data.mip_levels[0].size = (std::size_t)width * (std::size_t)height * 4;
    imgDesc.label = "landscape3d-grass";
    m_grassImage = sg_make_image(&imgDesc);
    if (pixels) {
        stbi_image_free(pixels);
    }

    if (m_grassImage.id == SG_INVALID_ID) {
        return false;
    }

    sg_view_desc viewDesc = {};
    viewDesc.texture.image = m_grassImage;
    m_grassView = sg_make_view(&viewDesc);

    sg_sampler_desc samplerDesc = {};
    samplerDesc.min_filter = SG_FILTER_NEAREST;
    samplerDesc.mag_filter = SG_FILTER_NEAREST;
    samplerDesc.wrap_u = SG_WRAP_REPEAT;
    samplerDesc.wrap_v = SG_WRAP_REPEAT;
    samplerDesc.label = "landscape3d-grass-sampler";
    m_grassSampler = sg_make_sampler(&samplerDesc);

    return loadedFromFile && m_grassView.id != SG_INVALID_ID && m_grassSampler.id != SG_INVALID_ID;
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
                        pushQuad(vertices, indices, p010, p110, p011, p111, {0.0f, 1.0f, 0.0f}, topColor, FaceKind::Top);
                    }

                    if (heightAtOrZero(x, z - 1) <= level) {
                        pushQuad(vertices, indices, p000, p100, p010, p110, {0.0f, 0.0f, -1.0f}, sideColor, FaceKind::Side, true);
                    }
                    if (heightAtOrZero(x + 1, z) <= level) {
                        pushQuad(vertices, indices, p100, p101, p110, p111, {1.0f, 0.0f, 0.0f}, sideColor, FaceKind::Side, true);
                    }
                    if (heightAtOrZero(x, z + 1) <= level) {
                        pushQuad(vertices, indices, p101, p001, p111, p011, {0.0f, 0.0f, 1.0f}, sideColor, FaceKind::Side, true);
                    }
                    if (heightAtOrZero(x - 1, z) <= level) {
                        pushQuad(vertices, indices, p001, p000, p011, p010, {-1.0f, 0.0f, 0.0f}, sideColor, FaceKind::Side, true);
                    }

                    pushBoxWireframe(lineVertices, p000, p100, p010, p110, p001, p101, p011, p111);
                }
            }
        }
    } else {
        for (int z = 0; z < grid; z++) {
            for (int x = 0; x < grid; x++) {
                const LandscapeTileType type = params.previewTileIndex >= 0
                    ? tileTypeFromAtlasIndex(params.previewTileIndex)
                    : scene.tileTypeAt(x, z);
                addTileStat(m_tileStats, type);
                if (!tileTypeHasSurface(type)) {
                    continue;
                }
                addValleyTileObject(vertices, indices, lineVertices, x, z, grid, scene, params, type);
            }
        }
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

    sg_buffer_desc lbuf = {};
    lbuf.usage.vertex_buffer = true;
    lbuf.data.ptr = lineVertices.data();
    lbuf.data.size = lineVertices.size() * sizeof(TerrainVertex);
    lbuf.label = "landscape3d-cube-lines";
    m_lineVertexBuffer = sg_make_buffer(&lbuf);

    m_terrainBindings = {};
    m_terrainBindings.vertex_buffers[0] = m_vertexBuffer;
    m_terrainBindings.index_buffer = m_indexBuffer;
    if (m_grassView.id != SG_INVALID_ID && m_grassSampler.id != SG_INVALID_ID) {
        m_terrainBindings.views[0] = m_grassView;
        m_terrainBindings.samplers[0] = m_grassSampler;
    }
    m_lineBindings = {};
    m_lineBindings.vertex_buffers[0] = m_lineVertexBuffer;
    if (m_grassView.id != SG_INVALID_ID && m_grassSampler.id != SG_INVALID_ID) {
        m_lineBindings.views[0] = m_grassView;
        m_lineBindings.samplers[0] = m_grassSampler;
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

    sg_range vsRange = {&vs, sizeof(vs)};
    sg_range fsRange = {&fs, sizeof(fs)};

    sg_apply_pipeline(m_terrainPipeline);
    sg_apply_bindings(&m_terrainBindings);
    sg_apply_uniforms(0, &vsRange);
    sg_apply_uniforms(1, &fsRange);
    sg_draw(0, m_indexCount, 1);

    if (params.showWireframe && m_linePipeline.id != SG_INVALID_ID && m_lineVertexCount > 0) {
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

