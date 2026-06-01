#include "MeshPreview.h"

#include "PlaygroundState.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <limits>
#include <mutex>
#include <random>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

#include <render_core/mesh_preview_renderer.h>
#include <sokol_app.h>
#include <sokol_gfx.h>
#include <util/sokol_imgui.h>
#include <spdlog/spdlog.h>

#include <stb_image.h>

namespace meshgen_playground {

namespace {

struct PreviewTexture {
    sg_image image{SG_INVALID_ID};
    sg_view view{SG_INVALID_ID};
    sg_sampler sampler{SG_INVALID_ID};
    bool loadedFromFile = false;
};

struct ProductionTextureState {
    PreviewTexture grass;
    PreviewTexture rock;
};

ProductionTextureState g_textures;
render_core::MeshPreviewRenderer g_gpuPreviewRenderer;

// ---- Throwaway test: 2D environment sprites placed on the 3D landscape ----
struct EnvSprite {
    PreviewTexture tex;
    int pixelWidth = 1;
    int pixelHeight = 1;
    float worldWidth = 1.0f; // world units, from index.json "image.width"
    bool isTree = false;     // trees/bushes prefer flat low ground in the scatter
    std::string name;
};

struct EnvPlacement {
    int sprite = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float scale = 1.0f;
};

std::vector<EnvSprite> g_envSprites;
std::vector<EnvPlacement> g_envPlacements;
bool g_envReseedRequested = true;

bool validTexture(const PreviewTexture& texture) {
    return texture.view.id != SG_INVALID_ID && texture.sampler.id != SG_INVALID_ID;
}

bool looksLikeDataRoot(const std::filesystem::path& dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    return fs::exists(dir / "src" / "apps" / "SplattingPlayground" / "resources" / "materials" / "grass.png", ec) &&
        fs::exists(dir / "resources" / "textures" / "rock_3.jpg", ec);
}

std::filesystem::path findDataRootUpwards(std::filesystem::path startDir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    startDir = fs::weakly_canonical(startDir, ec);
    if (startDir.empty()) {
        startDir = fs::current_path(ec);
    }
    if (startDir.empty()) {
        return {};
    }

    fs::path dir = startDir;
    for (int i = 0; i < 16; i++) {
        if (looksLikeDataRoot(dir)) {
            return dir;
        }
        if (!dir.has_parent_path()) {
            break;
        }
        const fs::path parent = dir.parent_path();
        if (parent == dir) {
            break;
        }
        dir = parent;
    }

    return {};
}

void destroyTexture(PreviewTexture& texture) {
    if (texture.sampler.id != SG_INVALID_ID) {
        sg_destroy_sampler(texture.sampler);
    }
    if (texture.view.id != SG_INVALID_ID) {
        sg_destroy_view(texture.view);
    }
    if (texture.image.id != SG_INVALID_ID) {
        sg_destroy_image(texture.image);
    }
    texture = {};
}

bool loadPreviewTexture(
    const std::filesystem::path& path,
    const std::uint8_t* fallbackPixels,
    int fallbackWidth,
    int fallbackHeight,
    const char* textureName,
    const char* imageLabel,
    const char* samplerLabel,
    PreviewTexture& texture) {

    destroyTexture(texture);

    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    const bool loadedFromFile = pixels != nullptr && width > 0 && height > 0;

    if (!loadedFromFile) {
        spdlog::warn("MeshPreview: failed to load {} texture '{}', using fallback", textureName, path.string());
        if (pixels != nullptr) {
            stbi_image_free(pixels);
            pixels = nullptr;
        }
        width = fallbackWidth;
        height = fallbackHeight;
    }

    sg_image_desc imageDesc = {};
    imageDesc.width = width;
    imageDesc.height = height;
    imageDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    imageDesc.data.mip_levels[0].ptr = loadedFromFile ? pixels : fallbackPixels;
    imageDesc.data.mip_levels[0].size = (std::size_t)width * (std::size_t)height * 4;
    imageDesc.label = imageLabel;
    texture.image = sg_make_image(&imageDesc);

    if (pixels != nullptr) {
        stbi_image_free(pixels);
    }

    if (texture.image.id == SG_INVALID_ID) {
        spdlog::error("MeshPreview: sg_make_image failed for {} texture", textureName);
        texture = {};
        return false;
    }

    sg_view_desc viewDesc = {};
    viewDesc.texture.image = texture.image;
    texture.view = sg_make_view(&viewDesc);

    sg_sampler_desc samplerDesc = {};
    samplerDesc.min_filter = SG_FILTER_LINEAR;
    samplerDesc.mag_filter = SG_FILTER_LINEAR;
    samplerDesc.wrap_u = SG_WRAP_REPEAT;
    samplerDesc.wrap_v = SG_WRAP_REPEAT;
    samplerDesc.label = samplerLabel;
    texture.sampler = sg_make_sampler(&samplerDesc);
    texture.loadedFromFile = loadedFromFile && validTexture(texture);

    if (!validTexture(texture)) {
        spdlog::error("MeshPreview: failed to create {} texture view or sampler", textureName);
        destroyTexture(texture);
        return false;
    }

    spdlog::info("MeshPreview: {} texture {} '{}'",
        textureName,
        texture.loadedFromFile ? "loaded from" : "using fallback for",
        path.string());
    return texture.loadedFromFile;
}

ImTextureID textureId(const PreviewTexture& texture) {
    return (ImTextureID)simgui_imtextureid_with_sampler(texture.view, texture.sampler);
}

ImTextureID textureId(sg_view view, sg_sampler sampler) {
    return (ImTextureID)simgui_imtextureid_with_sampler(view, sampler);
}

// Minimal scanner for a numeric value following "key": in a flat JSON file.
// Good enough for the simple environment index.json files; avoids pulling a JSON
// dependency into this throwaway test.
float parseJsonNumber(const std::string& text, const std::string& key, float fallback) {
    const std::string token = "\"" + key + "\"";
    std::size_t pos = text.find(token);
    if (pos == std::string::npos) {
        return fallback;
    }
    pos = text.find(':', pos + token.size());
    if (pos == std::string::npos) {
        return fallback;
    }
    pos++;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\n' || text[pos] == '\r')) {
        pos++;
    }
    char* end = nullptr;
    const double value = std::strtod(text.c_str() + pos, &end);
    if (end == text.c_str() + pos) {
        return fallback;
    }
    return (float)value;
}

bool loadSpriteTexture(const std::filesystem::path& path, EnvSprite& sprite) {
    int width = 0;
    int height = 0;
    int channels = 0;
    stbi_uc* pixels = stbi_load(path.string().c_str(), &width, &height, &channels, 4);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        if (pixels != nullptr) {
            stbi_image_free(pixels);
        }
        return false;
    }

    sg_image_desc imageDesc = {};
    imageDesc.width = width;
    imageDesc.height = height;
    imageDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    imageDesc.data.mip_levels[0].ptr = pixels;
    imageDesc.data.mip_levels[0].size = (std::size_t)width * (std::size_t)height * 4;
    imageDesc.label = "meshgen-env-sprite";
    sprite.tex.image = sg_make_image(&imageDesc);
    stbi_image_free(pixels);

    if (sprite.tex.image.id == SG_INVALID_ID) {
        return false;
    }

    sg_view_desc viewDesc = {};
    viewDesc.texture.image = sprite.tex.image;
    sprite.tex.view = sg_make_view(&viewDesc);

    sg_sampler_desc samplerDesc = {};
    samplerDesc.min_filter = SG_FILTER_LINEAR;
    samplerDesc.mag_filter = SG_FILTER_LINEAR;
    samplerDesc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    samplerDesc.label = "meshgen-env-sprite-sampler";
    sprite.tex.sampler = sg_make_sampler(&samplerDesc);
    sprite.tex.loadedFromFile = validTexture(sprite.tex);
    sprite.pixelWidth = width;
    sprite.pixelHeight = height;
    return validTexture(sprite.tex);
}

void loadEnvironmentSprites(const std::filesystem::path& dataRoot) {
    namespace fs = std::filesystem;
    for (EnvSprite& sprite : g_envSprites) {
        destroyTexture(sprite.tex);
    }
    g_envSprites.clear();

    const fs::path envRoot = dataRoot / "resources" / "assets" / "environment";
    std::error_code ec;
    if (!fs::exists(envRoot, ec)) {
        spdlog::warn("MeshPreview: environment assets root not found '{}'", envRoot.string());
        return;
    }

    for (const auto& entry : fs::directory_iterator(envRoot, ec)) {
        if (!entry.is_directory()) {
            continue;
        }
        const fs::path indexPath = entry.path() / "index.json";
        const fs::path imagePath = entry.path() / "image.png";
        if (!fs::exists(indexPath, ec) || !fs::exists(imagePath, ec)) {
            continue;
        }

        std::ifstream file(indexPath);
        std::stringstream buffer;
        buffer << file.rdbuf();
        const std::string text = buffer.str();

        EnvSprite sprite;
        sprite.name = entry.path().filename().string();
        sprite.worldWidth = std::max(0.2f, parseJsonNumber(text, "width", 1.0f));
        sprite.isTree = sprite.name.find("Tree") != std::string::npos ||
            sprite.name.find("Bush") != std::string::npos;
        if (!loadSpriteTexture(imagePath, sprite)) {
            spdlog::warn("MeshPreview: failed to load env sprite '{}'", imagePath.string());
            continue;
        }
        g_envSprites.push_back(std::move(sprite));
    }

    spdlog::info("MeshPreview: loaded {} environment sprites from '{}'", g_envSprites.size(), envRoot.string());
}

void regenerateEnvPlacements(const LandscapeBowlSettings& settings, const LandscapeBowlModel& model) {
    g_envPlacements.clear();
    if (g_envSprites.empty() || model.heights.empty()) {
        return;
    }

    std::mt19937 rng((unsigned)settings.seed * 2654435761u + 12345u);
    std::uniform_real_distribution<float> jitter(-0.32f, 0.32f);
    std::uniform_real_distribution<float> scaleDist(0.82f, 1.18f);
    std::uniform_int_distribution<int> spriteDist(0, (int)g_envSprites.size() - 1);

    const int gw = settings.gridWidth;
    const int gh = settings.gridHeight;
    const int target = std::min(160, std::max(20, gw * gh / 6));
    for (int i = 0; i < target; i++) {
        const int cx = (int)(rng() % (unsigned)gw);
        const int cz = (int)(rng() % (unsigned)gh);
        const std::size_t idx = (std::size_t)landscapeIndex(cx, cz, gw);
        if (idx >= model.heights.size()) {
            continue;
        }
        EnvPlacement placement;
        placement.sprite = spriteDist(rng);
        placement.x = (float)cx + 0.5f + jitter(rng);
        placement.z = (float)cz + 0.5f + jitter(rng);
        placement.y = model.heights[idx];
        placement.scale = scaleDist(rng);
        g_envPlacements.push_back(placement);
    }

    spdlog::info("MeshPreview: scattered {} environment sprite placements", g_envPlacements.size());
}

ImU32 colorForVertex(VertexKind kind) {
    switch (kind) {
    case VertexKind::OuterCorner:
        return IM_COL32(250, 196, 72, 255);
    case VertexKind::InnerCorner:
        return IM_COL32(232, 92, 92, 255);
    case VertexKind::DiagonalJoin:
        return IM_COL32(186, 118, 255, 255);
    case VertexKind::Edge:
        return IM_COL32(92, 180, 255, 255);
    case VertexKind::Empty:
    case VertexKind::SolidInterior:
    default:
        return IM_COL32(160, 160, 160, 255);
    }
}

const char* labelForVertex(VertexKind kind) {
    switch (kind) {
    case VertexKind::OuterCorner:
        return "O";
    case VertexKind::InnerCorner:
        return "I";
    case VertexKind::DiagonalJoin:
        return "D";
    case VertexKind::Edge:
        return "E";
    case VertexKind::Empty:
    case VertexKind::SolidInterior:
    default:
        return "";
    }
}

ImVec2 gridPointToScreen(const ImVec2& origin, float cellSize, const Int2& point) {
    return {origin.x + (float)point.x * cellSize, origin.y + (float)point.y * cellSize};
}

ImVec2 meshProjectionAnchor(const ImVec2& origin, const ImVec2& canvasSize, const MeshPreviewCamera& camera) {
    return {
        origin.x + canvasSize.x * 0.5f + camera.pan.x,
        origin.y + 130.0f + camera.pan.y,
    };
}

Vec3 subtract(const Vec3& lhs, const Vec3& rhs) {
    return {
        lhs.x - rhs.x,
        lhs.y - rhs.y,
        lhs.z - rhs.z,
    };
}

Vec3 add(const Vec3& lhs, const Vec3& rhs) {
    return {
        lhs.x + rhs.x,
        lhs.y + rhs.y,
        lhs.z + rhs.z,
    };
}

Vec3 scale(const Vec3& value, float factor) {
    return {
        value.x * factor,
        value.y * factor,
        value.z * factor,
    };
}

float dot(const Vec3& lhs, const Vec3& rhs) {
    return lhs.x * rhs.x + lhs.y * rhs.y + lhs.z * rhs.z;
}

Vec3 cross(const Vec3& lhs, const Vec3& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

Vec3 normalize(const Vec3& value) {
    const float length = std::sqrt(dot(value, value));
    if (length <= 0.0001f) {
        return {0.0f, 1.0f, 0.0f};
    }

    return {
        value.x / length,
        value.y / length,
        value.z / length,
    };
}

Vec3 defaultProductionLightDirection() {
    return normalize({-0.35f, 0.82f, -0.45f});
}

Vec3 rotateLightDirectionFromDrag(const Vec3& direction, const ImVec2& mouseDelta) {
    constexpr float kDragSensitivity = 0.012f;
    constexpr float kMinElevation = 0.12f;
    constexpr float kMaxElevation = 1.35f;

    const Vec3 current = normalize(direction);
    const float horizontalLength = std::max(0.0001f, std::sqrt(current.x * current.x + current.z * current.z));
    float azimuth = std::atan2(current.z, current.x);
    float elevation = std::atan2(current.y, horizontalLength);

    azimuth += mouseDelta.x * kDragSensitivity;
    elevation = clampFloat(elevation - mouseDelta.y * kDragSensitivity, kMinElevation, kMaxElevation);

    const float horizontal = std::cos(elevation);
    return normalize({
        horizontal * std::cos(azimuth),
        std::sin(elevation),
        horizontal * std::sin(azimuth),
    });
}

Vec3 areaWeightedFaceNormal(const MeshQuad& quad) {
    const Vec3 firstTriangle = cross(subtract(quad.b, quad.a), subtract(quad.c, quad.a));
    const Vec3 secondTriangle = cross(subtract(quad.c, quad.a), subtract(quad.d, quad.a));
    return normalize(add(firstTriangle, secondTriangle));
}

Vec3 topNormal(const MeshQuad& quad) {
    Vec3 normal = areaWeightedFaceNormal(quad);
    if (normal.y < 0.0f) {
        normal = scale(normal, -1.0f);
    }
    return normal;
}

Vec3 wallNormal(const MeshQuad& quad) {
    const Vec3 along = scale(add(subtract(quad.b, quad.a), subtract(quad.c, quad.d)), 0.5f);
    const Vec3 down = scale(add(subtract(quad.d, quad.a), subtract(quad.c, quad.b)), 0.5f);
    return normalize(cross(along, down));
}

Vec3 generatedNormal(const MeshQuad& quad) {
    return normalize(quad.normal);
}

ImU32 shadeColor(ImU32 color, float factor) {
    const int r = (color >> IM_COL32_R_SHIFT) & 0xff;
    const int g = (color >> IM_COL32_G_SHIFT) & 0xff;
    const int b = (color >> IM_COL32_B_SHIFT) & 0xff;
    const int a = (color >> IM_COL32_A_SHIFT) & 0xff;

    return IM_COL32(
        clampInt((int)((float)r * factor), 0, 255),
        clampInt((int)((float)g * factor), 0, 255),
        clampInt((int)((float)b * factor), 0, 255),
        a);
}

ImU32 blendTowardWhite(ImU32 color, float amount) {
    amount = clampFloat(amount, 0.0f, 1.0f);
    const int r = (color >> IM_COL32_R_SHIFT) & 0xff;
    const int g = (color >> IM_COL32_G_SHIFT) & 0xff;
    const int b = (color >> IM_COL32_B_SHIFT) & 0xff;
    const int a = (color >> IM_COL32_A_SHIFT) & 0xff;

    return IM_COL32(
        clampInt((int)((float)r + (255.0f - (float)r) * amount), 0, 255),
        clampInt((int)((float)g + (255.0f - (float)g) * amount), 0, 255),
        clampInt((int)((float)b + (255.0f - (float)b) * amount), 0, 255),
        a);
}

ImU32 blendTowardColor(ImU32 color, ImU32 target, float amount) {
    amount = clampFloat(amount, 0.0f, 1.0f);
    const int r = (color >> IM_COL32_R_SHIFT) & 0xff;
    const int g = (color >> IM_COL32_G_SHIFT) & 0xff;
    const int b = (color >> IM_COL32_B_SHIFT) & 0xff;
    const int a = (color >> IM_COL32_A_SHIFT) & 0xff;
    const int targetR = (target >> IM_COL32_R_SHIFT) & 0xff;
    const int targetG = (target >> IM_COL32_G_SHIFT) & 0xff;
    const int targetB = (target >> IM_COL32_B_SHIFT) & 0xff;

    return IM_COL32(
        clampInt((int)((float)r + ((float)targetR - (float)r) * amount), 0, 255),
        clampInt((int)((float)g + ((float)targetG - (float)g) * amount), 0, 255),
        clampInt((int)((float)b + ((float)targetB - (float)b) * amount), 0, 255),
        a);
}

ImU32 previewWallColor(ImU32 color) {
    return blendTowardColor(color, IM_COL32(118, 110, 96, 255), 0.68f);
}

glm::vec4 colorToVec4(ImU32 color) {
    return {
        (float)((color >> IM_COL32_R_SHIFT) & 0xff) / 255.0f,
        (float)((color >> IM_COL32_G_SHIFT) & 0xff) / 255.0f,
        (float)((color >> IM_COL32_B_SHIFT) & 0xff) / 255.0f,
        (float)((color >> IM_COL32_A_SHIFT) & 0xff) / 255.0f,
    };
}

ImU32 normalDebugColor(const Vec3& normal) {
    return IM_COL32(
        clampInt((int)((normal.x * 0.5f + 0.5f) * 255.0f), 0, 255),
        clampInt((int)((normal.y * 0.5f + 0.5f) * 255.0f), 0, 255),
        clampInt((int)((normal.z * 0.5f + 0.5f) * 255.0f), 0, 255),
        255);
}

float cliffProximity(const MeshQuad& quad, const ProductionPreviewSettings& previewSettings) {
    if (quad.cliffWall) {
        return 1.0f;
    }
    const float radius = std::max(0.001f, previewSettings.cliffDarkeningRadius);
    return clampFloat(1.0f - quad.cliffDistance / radius, 0.0f, 1.0f);
}

Vec3 wallAlongDirection(const MeshQuad& quad);

Vec3 stableWallNormal(const MeshQuad& quad) {
    const Vec3 raw = generatedNormal(quad);
    const Vec3 along = wallAlongDirection(quad);
    Vec3 normal{-along.z, 0.0f, along.x};
    if (dot(normal, raw) < 0.0f) {
        normal = scale(normal, -1.0f);
    }

    const float length = std::sqrt(normal.x * normal.x + normal.z * normal.z);
    if (length <= 0.0001f) {
        return {0.0f, 0.0f, 1.0f};
    }

    return {
        normal.x / length,
        0.0f,
        normal.z / length,
    };
}

Vec3 blendedWallNormal(const MeshQuad& quad) {
    const Vec3 stable = generatedNormal(quad);
    Vec3 detailed = wallNormal(quad);
    if (dot(stable, detailed) < 0.0f) {
        detailed = scale(detailed, -1.0f);
    }
    detailed.y = clampFloat(detailed.y, -0.35f, 0.35f);

    constexpr float kDetailInfluence = 0.18f;
    return normalize(add(scale(stable, 1.0f - kDetailInfluence), scale(detailed, kDetailInfluence)));
}

Vec3 wallPlaneNormal(const MeshQuad& quad) {
    const Vec3 n = generatedNormal(quad);
    const float length = std::sqrt(n.x * n.x + n.z * n.z);
    if (length < 0.0001f) {
        return n;
    }
    return {n.x / length, 0.0f, n.z / length};
}

// A1: interpolate the wall normal between the flat cliff plane (detail = 0)
// and the full faceted displacement normal (detail = 1).
Vec3 detailAdjustedWallNormal(const MeshQuad& quad, float detail) {
    detail = clampFloat(detail, 0.0f, 1.0f);
    const Vec3 face = generatedNormal(quad);
    if (detail >= 0.999f) {
        return face;
    }
    const Vec3 plane = wallPlaneNormal(quad);
    return normalize(add(scale(plane, 1.0f - detail), scale(face, detail)));
}

Vec3 previewNormalForQuad(const MeshQuad& quad, const ProductionPreviewSettings& previewSettings) {
    const ProductionPreviewDebugMode debugMode = (ProductionPreviewDebugMode)previewSettings.debugMode;
    if (debugMode == ProductionPreviewDebugMode::RawNormals) {
        return generatedNormal(quad);
    }

    if (!quad.cliffWall) {
        return topNormal(quad);
    }

    if (debugMode == ProductionPreviewDebugMode::StableNormals) {
        return stableWallNormal(quad);
    }
    if (debugMode == ProductionPreviewDebugMode::BlendedNormals) {
        return blendedWallNormal(quad);
    }
    return detailAdjustedWallNormal(quad, previewSettings.wallDetailNormal);
}

ImU32 productionLitColor(
    const MeshQuad& quad,
    const Vec3& lightDirection,
    const ProductionPreviewSettings& previewSettings) {
    const Vec3 normal = previewNormalForQuad(quad, previewSettings);

    const ProductionPreviewDebugMode debugMode = (ProductionPreviewDebugMode)previewSettings.debugMode;
    if (debugMode == ProductionPreviewDebugMode::Albedo) {
        return quad.cliffWall ? previewWallColor(quad.color) : blendTowardWhite(quad.color, 0.48f);
    }
    if (debugMode == ProductionPreviewDebugMode::RawNormals ||
        debugMode == ProductionPreviewDebugMode::StableNormals ||
        debugMode == ProductionPreviewDebugMode::BlendedNormals) {
        return normalDebugColor(normal);
    }
    if (debugMode == ProductionPreviewDebugMode::CliffProximity) {
        const int shade = clampInt((int)(cliffProximity(quad, previewSettings) * 255.0f), 0, 255);
        return IM_COL32(shade, shade, shade, 255);
    }
    if (debugMode == ProductionPreviewDebugMode::DepthOrder) {
        const float t = clampFloat((quad.depth + 32.0f) / 96.0f, 0.0f, 1.0f);
        return IM_COL32(
            clampInt((int)(t * 255.0f), 0, 255),
            64,
            clampInt((int)((1.0f - t) * 255.0f), 0, 255),
            255);
    }

    if (quad.cliffWall) {
        const float wrappedLambert = clampFloat(dot(normal, lightDirection) * 0.5f + 0.5f, 0.0f, 1.0f);
        float lighting = previewSettings.wallBrightness *
            (previewSettings.ambient + previewSettings.diffuseStrength * wrappedLambert);

        const float ridge = clampFloat((quad.relief - 0.16f) / 0.69f, 0.0f, 1.0f);
        const float crevice = clampFloat((-quad.relief - 0.16f) / 0.69f, 0.0f, 1.0f);
        ImU32 wallColorOut = previewWallColor(quad.color);
        wallColorOut = blendTowardWhite(wallColorOut, ridge * previewSettings.wallEdgeWearStrength * 0.35f);
        wallColorOut = blendTowardColor(wallColorOut, IM_COL32(70, 80, 60, 255), crevice * previewSettings.wallCreviceStrength);

        const float baseAo = clampFloat(quad.heightFraction / 0.45f, 0.0f, 1.0f);
        const float ao = (1.0f - previewSettings.wallAoStrength) + previewSettings.wallAoStrength * baseAo;
        lighting *= ao * (1.0f - crevice * previewSettings.wallCreviceStrength * 0.6f);
        return shadeColor(wallColorOut, lighting);
    }

    const float lambert = std::max(0.0f, dot(normal, lightDirection));
    float lighting = previewSettings.ambient + previewSettings.diffuseStrength * lambert;
    const float proximity = cliffProximity(quad, previewSettings);
    const float topDarkening = std::max(
        previewSettings.minTopBrightness,
        1.0f - previewSettings.cliffDarkeningStrength * proximity);
    lighting *= topDarkening * (1.0f - previewSettings.edgeDarkness * proximity);
    return shadeColor(blendTowardWhite(quad.color, 0.50f), lighting);
}

ImVec2 grassUv(const Vec3& point) {
    constexpr float kGrassUvScale = 0.5f;
    return {point.x * kGrassUvScale, point.z * kGrassUvScale};
}

Vec3 wallAlongDirection(const MeshQuad& quad) {
    const Vec3 along = scale(add(subtract(quad.b, quad.a), subtract(quad.c, quad.d)), 0.5f);
    const float horizontalLength = std::sqrt(along.x * along.x + along.z * along.z);
    if (horizontalLength <= 0.0001f) {
        return {1.0f, 0.0f, 0.0f};
    }

    return {along.x / horizontalLength, 0.0f, along.z / horizontalLength};
}

ImVec2 wallUv(const MeshQuad& quad, const Vec3& point) {
    constexpr float kRockAlongScale = 0.35f;
    constexpr float kRockHeightScale = 0.42f;
    const Vec3 along = wallAlongDirection(quad);
    const float u = (point.x * along.x + point.z * along.z) * kRockAlongScale;
    return {u, -point.y * kRockHeightScale};
}

ImVec2 scaleUv(ImVec2 uv, float scaleValue) {
    return {uv.x * scaleValue, uv.y * scaleValue};
}

glm::vec3 toGlm(const Vec3& value) {
    return {value.x, value.y, value.z};
}

glm::vec2 toGlm(const ImVec2& value) {
    return {value.x, value.y};
}

render_core::MeshPreviewQuad toRenderQuad(const MeshQuad& quad, const ProductionPreviewSettings& previewSettings) {
    render_core::MeshPreviewQuad result;
    result.a = toGlm(quad.a);
    result.b = toGlm(quad.b);
    result.c = toGlm(quad.c);
    result.d = toGlm(quad.d);
    result.normal = toGlm(previewNormalForQuad(quad, previewSettings));
    result.color = colorToVec4(quad.cliffWall ? previewWallColor(quad.color) : blendTowardWhite(quad.color, 0.50f));
    result.uvA = toGlm(quad.cliffWall ? wallUv(quad, quad.a) : grassUv(quad.a));
    result.uvB = toGlm(quad.cliffWall ? wallUv(quad, quad.b) : grassUv(quad.b));
    result.uvC = toGlm(quad.cliffWall ? wallUv(quad, quad.c) : grassUv(quad.c));
    result.uvD = toGlm(quad.cliffWall ? wallUv(quad, quad.d) : grassUv(quad.d));
    result.faceKind = quad.cliffWall ? 1.0f : 0.0f;
    result.cliffDistance = quad.cliffDistance;
    result.depth = quad.depth;

    if ((ProductionPreviewDebugMode)previewSettings.debugMode == ProductionPreviewDebugMode::DepthOrder) {
        const float t = clampFloat((quad.depth + 32.0f) / 96.0f, 0.0f, 1.0f);
        result.color = {t, 0.25f, 1.0f - t, 1.0f};
    }

    return result;
}

std::vector<render_core::MeshPreviewQuad> buildRenderQuads(
    const LandscapeBowlSettings& settings,
    const LandscapeBowlModel& model,
    const ProductionPreviewSettings& previewSettings) {

    std::vector<render_core::MeshPreviewQuad> quads;
    quads.reserve(model.meshQuads.size());
    for (const MeshQuad& quad : model.meshQuads) {
        if (quad.cliffWall && !settings.showCliffWalls) {
            continue;
        }
        if (!quad.cliffWall && !settings.showTopFaces) {
            continue;
        }
        quads.push_back(toRenderQuad(quad, previewSettings));
    }
    return quads;
}

render_core::MeshPreviewRenderParams makeRenderParams(
    const LandscapeBowlSettings& settings,
    const MeshPreviewCamera& camera,
    const ImVec2& viewportSize,
    const Vec3& lightDirection,
    const ProductionPreviewSettings& previewSettings) {

    render_core::MeshPreviewRenderParams params;
    params.worldCenter = {(float)settings.gridWidth * 0.5f, (float)settings.gridHeight * 0.5f};
    params.pan = {camera.pan.x, camera.pan.y};
    params.lightDirection = toGlm(lightDirection);
    params.zoom = camera.zoom;
    params.anchorY = std::min(170.0f, std::max(96.0f, viewportSize.y * 0.34f));
    params.isoScaleX = 19.0f;
    params.isoScaleY = 9.5f;
    params.heightScale = 25.0f;
    params.ambient = previewSettings.ambient;
    params.diffuse = previewSettings.diffuseStrength;
    params.wallBrightness = previewSettings.wallBrightness;
    params.textureScale = previewSettings.textureScale;
    params.cliffDarkeningRadius = previewSettings.cliffDarkeningRadius;
    params.cliffDarkeningStrength = previewSettings.cliffDarkeningStrength;
    params.minTopBrightness = previewSettings.minTopBrightness;
    params.edgeDarkness = previewSettings.edgeDarkness;
    params.macroScale = previewSettings.macroScale;
    params.macroStrength = previewSettings.macroStrength;
    params.wallAoStrength = previewSettings.wallAoStrength;
    params.wallEdgeWearStrength = previewSettings.wallEdgeWearStrength;
    params.wallCreviceStrength = previewSettings.wallCreviceStrength;
    params.wallGrainStrength = previewSettings.wallGrainStrength;
    params.wallFacetWearStrength = previewSettings.wallFacetWearStrength;
    params.wallFacetWearWidth = previewSettings.wallFacetWearWidth;
    params.debugMode = previewSettings.debugMode;
    return params;
}

ImVec2 projectMeshPoint(
    const RectangleCliffSettings& settings,
    const MeshPreviewCamera& camera,
    const ImVec2& origin,
    const ImVec2& canvasSize,
    const Vec3& point) {

    const float centeredX = point.x - (float)settings.gridWidth * 0.5f;
    const float centeredZ = point.z - (float)settings.gridHeight * 0.5f;
    const float isoX = (centeredX - centeredZ) * 28.0f * camera.zoom;
    const float isoY = ((centeredX + centeredZ) * 14.0f - point.y * 34.0f) * camera.zoom;
    const ImVec2 anchor = meshProjectionAnchor(origin, canvasSize, camera);
    return {
        anchor.x + isoX,
        anchor.y + isoY,
    };
}

void drawProjectedQuad(
    ImDrawList* drawList,
    const RectangleCliffSettings& settings,
    const MeshPreviewCamera& camera,
    const ImVec2& origin,
    const ImVec2& canvasSize,
    const MeshQuad& quad) {

    const ImVec2 a = projectMeshPoint(settings, camera, origin, canvasSize, quad.a);
    const ImVec2 b = projectMeshPoint(settings, camera, origin, canvasSize, quad.b);
    const ImVec2 c = projectMeshPoint(settings, camera, origin, canvasSize, quad.c);
    const ImVec2 d = projectMeshPoint(settings, camera, origin, canvasSize, quad.d);

    drawList->AddQuadFilled(a, b, c, d, quad.color);
    if (settings.showMeshWireframe) {
        drawList->AddQuad(a, b, c, d, IM_COL32(24, 24, 24, 210), 1.25f);
    }
}

ImVec2 projectLandscapeMeshPoint(
    const LandscapeBowlSettings& settings,
    const MeshPreviewCamera& camera,
    const ImVec2& origin,
    const ImVec2& canvasSize,
    const Vec3& point) {

    const float centeredX = point.x - (float)settings.gridWidth * 0.5f;
    const float centeredZ = point.z - (float)settings.gridHeight * 0.5f;
    const float isoX = (centeredX - centeredZ) * 19.0f * camera.zoom;
    const float isoY = ((centeredX + centeredZ) * 9.5f - point.y * 25.0f) * camera.zoom;
    const ImVec2 anchor = meshProjectionAnchor(origin, canvasSize, camera);
    return {
        anchor.x + isoX,
        anchor.y + isoY,
    };
}

void drawLandscapeProjectedQuad(
    ImDrawList* drawList,
    const LandscapeBowlSettings& settings,
    const MeshPreviewCamera& camera,
    const ImVec2& origin,
    const ImVec2& canvasSize,
    const MeshQuad& quad,
    const Vec3& lightDirection,
    const ProductionPreviewSettings& previewSettings) {

    const ImVec2 a = projectLandscapeMeshPoint(settings, camera, origin, canvasSize, quad.a);
    const ImVec2 b = projectLandscapeMeshPoint(settings, camera, origin, canvasSize, quad.b);
    const ImVec2 c = projectLandscapeMeshPoint(settings, camera, origin, canvasSize, quad.c);
    const ImVec2 d = projectLandscapeMeshPoint(settings, camera, origin, canvasSize, quad.d);

    const PreviewTexture& texture = quad.cliffWall ? g_textures.rock : g_textures.grass;
    const ImU32 tint = productionLitColor(quad, lightDirection, previewSettings);
    if (quad.cliffWall) {
        drawList->AddQuadFilled(a, b, c, d, tint);
        return;
    }
    if (!validTexture(texture)) {
        drawList->AddQuadFilled(a, b, c, d, tint);
        return;
    }

    const float uvScale = std::max(0.001f, previewSettings.textureScale);
    const ImVec2 uvA = scaleUv(quad.cliffWall ? wallUv(quad, quad.a) : grassUv(quad.a), uvScale);
    const ImVec2 uvB = scaleUv(quad.cliffWall ? wallUv(quad, quad.b) : grassUv(quad.b), uvScale);
    const ImVec2 uvC = scaleUv(quad.cliffWall ? wallUv(quad, quad.c) : grassUv(quad.c), uvScale);
    const ImVec2 uvD = scaleUv(quad.cliffWall ? wallUv(quad, quad.d) : grassUv(quad.d), uvScale);
    drawList->AddImageQuad(textureId(texture), a, b, c, d, uvA, uvB, uvC, uvD, tint);
}

ImU32 colorForLandscapeZone(LandscapeZone zone, float height, float minHeight, float maxHeight) {
    const float t = clampFloat((height - minHeight) / std::max(0.001f, maxHeight - minHeight), 0.0f, 1.0f);
    switch (zone) {
    case LandscapeZone::Clearing:
        return IM_COL32(110, 161, 89, 255);
    case LandscapeZone::HighGround:
        return IM_COL32(
            clampInt(105 + (int)(t * 55.0f), 80, 180),
            clampInt(120 + (int)(t * 46.0f), 90, 180),
            clampInt(88 + (int)(t * 34.0f), 70, 150),
            255);
    case LandscapeZone::Hill:
        return IM_COL32(126, 151, 91, 255);
    case LandscapeZone::Slope:
        return IM_COL32(96, 136, 84, 255);
    case LandscapeZone::Lowland:
    default:
        return IM_COL32(78, 128, 82, 255);
    }
}

} // namespace

void initProductionPreviewTextures() {
    static constexpr std::uint8_t grassFallback[] = {
        88, 137, 73, 255, 108, 158, 86, 255,
        99, 151, 80, 255, 76, 125, 68, 255,
    };
    static constexpr std::uint8_t rockFallback[] = {
        104, 99, 88, 255, 126, 119, 104, 255,
        82, 78, 72, 255, 112, 106, 96, 255,
    };

    std::error_code ec;
    std::filesystem::path dataRoot = findDataRootUpwards(std::filesystem::current_path(ec));
    if (dataRoot.empty()) {
        spdlog::warn("MeshPreview: failed to auto-detect data root, using current directory");
        dataRoot = std::filesystem::current_path(ec);
    }

    const std::filesystem::path grassPath = dataRoot / "src" / "apps" / "SplattingPlayground" / "resources" / "materials" / "grass.png";
    const std::filesystem::path rockPath = dataRoot / "resources" / "textures" / "rock_3.jpg";

    loadPreviewTexture(grassPath, grassFallback, 2, 2, "grass", "meshgen-grass", "meshgen-grass-sampler", g_textures.grass);
    loadPreviewTexture(rockPath, rockFallback, 2, 2, "rock", "meshgen-rock", "meshgen-rock-sampler", g_textures.rock);
    loadEnvironmentSprites(dataRoot);
    g_gpuPreviewRenderer.init();
}

void shutdownProductionPreviewTextures() {
    g_gpuPreviewRenderer.shutdown();
    destroyTexture(g_textures.grass);
    destroyTexture(g_textures.rock);
    for (EnvSprite& sprite : g_envSprites) {
        destroyTexture(sprite.tex);
    }
    g_envSprites.clear();
    g_envPlacements.clear();
}

int loadedEnvSpriteCount() {
    return (int)g_envSprites.size();
}

void requestEnvSpriteReseed() {
    g_envReseedRequested = true;
}

bool productionGrassTextureLoaded() {
    return g_textures.grass.loadedFromFile;
}

bool productionRockTextureLoaded() {
    return g_textures.rock.loadedFromFile;
}

bool warmupProductionPreviewRenderer() {
    LandscapeBowlSettings settings;
    LandscapeBowlModel model;
    MeshPreviewCamera camera;
    ProductionPreviewSettings previewSettings;
    Vec3 lightDirection;
    {
        std::lock_guard<std::mutex> modelLock(g_modelMutex);
        settings = g_landscapeSettings;
        model = g_landscapeModel;
        previewSettings = g_productionPreviewSettings;
    }
    {
        std::lock_guard<std::mutex> stateLock(g_stateMutex);
        camera = g_landscapeCamera;
        lightDirection = normalize(g_productionLightDirection);
    }

    const ImVec2 warmupSize{640.0f, 360.0f};
    const std::vector<render_core::MeshPreviewQuad> renderQuads = buildRenderQuads(settings, model, previewSettings);
    const bool ok = g_gpuPreviewRenderer.render(
        renderQuads,
        makeRenderParams(settings, camera, warmupSize, lightDirection, previewSettings),
        g_textures.grass.view,
        g_textures.rock.view,
        g_textures.grass.sampler,
        (int)warmupSize.x,
        (int)warmupSize.y);

    if (ok && g_gpuPreviewRenderer.validOutput()) {
        spdlog::info("TEST PASS MeshGenerationPlayground GPU preview renderer warmup: quads={}, output={}x{}",
            renderQuads.size(),
            (int)warmupSize.x,
            (int)warmupSize.y);
        return true;
    }

    spdlog::error("TEST FAIL MeshGenerationPlayground GPU preview renderer warmup: quads={}, outputValid={}",
        renderQuads.size(),
        g_gpuPreviewRenderer.validOutput());
    return false;
}

void drawRectangleCliffDebugView(const RectangleCliffSettings& settings, const RectangleCliffModel& model, const ImVec2& viewportSize) {
    const float cellSize = 34.0f;
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 gridOrigin{origin.x + 12.0f, origin.y + 36.0f};

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(20, 23, 28, 255));
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), "2D logical boundary view");
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            const ImVec2 min{gridOrigin.x + (float)x * cellSize, gridOrigin.y + (float)y * cellSize};
            const ImVec2 max{min.x + cellSize, min.y + cellSize};

            if (isSolidCell(model, settings, x, y)) {
                drawList->AddRectFilled({min.x + 1.0f, min.y + 1.0f}, {max.x - 1.0f, max.y - 1.0f}, IM_COL32(74, 114, 74, 255));
                if (settings.showCellLabels) {
                    drawList->AddText({min.x + 9.0f, min.y + 8.0f}, IM_COL32(215, 235, 205, 255), "land");
                }
            }

            drawList->AddRect(min, max, IM_COL32(58, 64, 74, 255));
        }
    }

    for (const BoundarySegment& segment : model.boundarySegments) {
        const ImVec2 a = gridPointToScreen(gridOrigin, cellSize, segment.a);
        const ImVec2 b = gridPointToScreen(gridOrigin, cellSize, segment.b);
        drawList->AddLine(a, b, IM_COL32(235, 106, 72, 255), 4.0f);
    }

    for (const VertexMarker& marker : model.vertexMarkers) {
        const ImVec2 center = gridPointToScreen(gridOrigin, cellSize, marker.position);
        const ImU32 color = colorForVertex(marker.kind);
        drawList->AddCircleFilled(center, 6.5f, color, 16);
        drawList->AddCircle(center, 7.5f, IM_COL32(10, 10, 10, 220), 16, 1.5f);

        if (settings.showVertexLabels) {
            drawList->AddText({center.x + 8.0f, center.y - 8.0f}, color, labelForVertex(marker.kind));
        }
    }

    drawList->PopClipRect();
    ImGui::Dummy(viewportSize);
}

void drawMesh3dPreview(const RectangleCliffSettings& settings, const RectangleCliffModel& model, const ImVec2& viewportSize) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##GeneratedMeshViewport", viewportSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    ImGuiIO& io = ImGui::GetIO();

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (hovered && io.MouseWheel != 0.0f) {
            const float previousZoom = g_meshCamera.zoom;
            const float nextZoom = clampFloat(previousZoom * (1.0f + io.MouseWheel * 0.12f), 0.35f, 12.0f);
            if (nextZoom != previousZoom) {
                const ImVec2 anchor = meshProjectionAnchor(origin, viewportSize, g_meshCamera);
                const ImVec2 mouseLocal{
                    io.MousePos.x - anchor.x,
                    io.MousePos.y - anchor.y,
                };
                const float zoomRatio = nextZoom / previousZoom;
                g_meshCamera.pan.x += mouseLocal.x * (1.0f - zoomRatio);
                g_meshCamera.pan.y += mouseLocal.y * (1.0f - zoomRatio);
                g_meshCamera.zoom = nextZoom;
            }
        }

        if (dragging) {
            g_meshCamera.pan.x += io.MouseDelta.x;
            g_meshCamera.pan.y += io.MouseDelta.y;
        }

        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            g_meshCamera.zoom = 1.0f;
            g_meshCamera.pan = {0.0f, 0.0f};
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();

    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(18, 21, 27, 255));
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    const ImVec2 groundCenter{origin.x + viewportSize.x * 0.5f, origin.y + 165.0f};
    drawList->AddEllipse(groundCenter, {360.0f, 150.0f}, IM_COL32(36, 40, 48, 255), 0.0f, 48, 2.0f);

    std::vector<int> drawOrder;
    drawOrder.reserve(model.meshQuads.size());
    for (int i = 0; i < (int)model.meshQuads.size(); i++) {
        const MeshQuad& quad = model.meshQuads[(std::size_t)i];
        if (quad.cliffWall && !settings.showCliffWalls) {
            continue;
        }
        if (!quad.cliffWall && !settings.showTopFaces) {
            continue;
        }
        drawOrder.push_back(i);
    }

    std::sort(drawOrder.begin(), drawOrder.end(), [&](int lhs, int rhs) {
        return model.meshQuads[(std::size_t)lhs].depth < model.meshQuads[(std::size_t)rhs].depth;
    });

    for (int index : drawOrder) {
        drawProjectedQuad(drawList, settings, g_meshCamera, origin, viewportSize, model.meshQuads[(std::size_t)index]);
    }

    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), "Generated 3D mesh: top quads + cliff wall quads");
    drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(150, 162, 180, 255), "LMB drag: pan, mouse wheel: zoom, double click: reset");
    char cameraText[96];
    snprintf(cameraText, sizeof(cameraText), "Camera zoom: %.2fx, pan: %.0f %.0f", g_meshCamera.zoom, g_meshCamera.pan.x, g_meshCamera.pan.y);
    drawList->AddText({origin.x + 12.0f, origin.y + 52.0f}, IM_COL32(150, 162, 180, 255), cameraText);
    drawList->PopClipRect();
}

void drawObjectMesh3dPreview(const ObjectGenerationSettings& settings, const ObjectGenerationModel& model, const ImVec2& viewportSize) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##ObjectMeshViewport", viewportSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    ImGuiIO& io = ImGui::GetIO();

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        if (hovered && io.MouseWheel != 0.0f) {
            g_objectCamera.zoom = clampFloat(g_objectCamera.zoom * (1.0f + io.MouseWheel * 0.12f), 0.25f, 16.0f);
        }

        if (dragging) {
            g_objectCamera.pan.x += io.MouseDelta.x;
            g_objectCamera.pan.y += io.MouseDelta.y;
        }

        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            g_objectCamera.zoom = 1.0f;
            g_objectCamera.pan = {0.0f, 0.0f};
        }
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(18, 21, 27, 255));
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    const ImVec2 groundCenter{origin.x + viewportSize.x * 0.5f, origin.y + viewportSize.y * 0.62f};
    drawList->AddEllipse(groundCenter, {220.0f, 90.0f}, IM_COL32(34, 39, 47, 255), 0.0f, 48, 2.0f);

    MeshPreviewCamera camera;
    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        camera = g_objectCamera;
    }

    const Vec3 lightDirection = normalize({-0.45f, 0.85f, -0.35f});
    auto projectObjectPoint = [&](const Vec3& point) {
        const float isoX = (point.x - point.z) * 42.0f * camera.zoom;
        const float isoY = ((point.x + point.z) * 21.0f - point.y * 54.0f) * camera.zoom;
        return ImVec2{
            groundCenter.x + camera.pan.x + isoX,
            groundCenter.y + camera.pan.y + isoY,
        };
    };

    std::vector<int> drawOrder;
    drawOrder.reserve(model.meshQuads.size());
    for (int i = 0; i < (int)model.meshQuads.size(); i++) {
        drawOrder.push_back(i);
    }

    std::sort(drawOrder.begin(), drawOrder.end(), [&](int lhs, int rhs) {
        return model.meshQuads[(std::size_t)lhs].depth < model.meshQuads[(std::size_t)rhs].depth;
    });

    for (int index : drawOrder) {
        const MeshQuad& quad = model.meshQuads[(std::size_t)index];
        const ImVec2 a = projectObjectPoint(quad.a);
        const ImVec2 b = projectObjectPoint(quad.b);
        const ImVec2 c = projectObjectPoint(quad.c);
        const ImVec2 d = projectObjectPoint(quad.d);
        const float lambert = std::max(0.0f, dot(normalize(quad.normal), lightDirection));
        const ImU32 color = shadeColor(quad.color, 0.62f + lambert * 0.48f);
        drawList->AddQuadFilled(a, b, c, d, color);
        if (settings.showMeshWireframe) {
            drawList->AddQuad(a, b, c, d, IM_COL32(18, 24, 32, 230), 1.4f);
        }
    }

    char titleText[128];
    snprintf(titleText, sizeof(titleText), "Generated object mesh: %s", objectGeneratorName(settings.generatorKind));
    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), titleText);
    drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(150, 162, 180, 255), "LMB drag: pan, mouse wheel: zoom, double click: reset");
    char cameraText[96];
    snprintf(cameraText, sizeof(cameraText), "Camera zoom: %.2fx, pan: %.0f %.0f", camera.zoom, camera.pan.x, camera.pan.y);
    drawList->AddText({origin.x + 12.0f, origin.y + 52.0f}, IM_COL32(150, 162, 180, 255), cameraText);
    drawList->PopClipRect();
}

void drawLandscapeBowlDebugView(const LandscapeBowlSettings& settings, const LandscapeBowlModel& model, const ImVec2& viewportSize) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float availableWidth = std::max(1.0f, viewportSize.x - 24.0f);
    const float availableHeight = std::max(1.0f, viewportSize.y - 48.0f);
    const float cellSize = std::min(availableWidth / (float)settings.gridWidth, availableHeight / (float)settings.gridHeight);
    const ImVec2 gridSize{cellSize * (float)settings.gridWidth, cellSize * (float)settings.gridHeight};
    const ImVec2 gridOrigin{
        origin.x + (viewportSize.x - gridSize.x) * 0.5f,
        origin.y + 36.0f + (availableHeight - gridSize.y) * 0.5f,
    };

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(20, 23, 28, 255));
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), "2D logical level map: heightmap quantized before tile composition");
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    for (int y = 0; y < settings.gridHeight; y++) {
        for (int x = 0; x < settings.gridWidth; x++) {
            const std::size_t index = (std::size_t)landscapeIndex(x, y, settings.gridWidth);
            const float height = model.heights[index];
            const LandscapeZone zone = model.zones[index];
            const ImVec2 min{gridOrigin.x + (float)x * cellSize, gridOrigin.y + (float)y * cellSize};
            const ImVec2 max{min.x + cellSize + 0.5f, min.y + cellSize + 0.5f};
            drawList->AddRectFilled(min, max, colorForLandscapeZone(zone, height, model.minHeight, model.maxHeight));
            if (cellSize >= 13.0f) {
                drawList->AddRect(min, max, IM_COL32(25, 30, 30, 90));
            }
            if (settings.showHeightValues && cellSize >= 18.0f) {
                char label[16];
                snprintf(label, sizeof(label), "L%d", landscapeLevelAtCell(model, settings, x, y));
                drawList->AddText({min.x + 2.0f, min.y + 2.0f}, IM_COL32(230, 235, 220, 220), label);
            }
        }
    }

    drawList->PopClipRect();
    ImGui::Dummy(viewportSize);
}

void drawLandscapeMesh3dPreview(const LandscapeBowlSettings& settings, const LandscapeBowlModel& model, const ImVec2& viewportSize) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##LandscapeMeshViewport", viewportSize);
    const bool hovered = ImGui::IsItemHovered();
    const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left);
    ImGuiIO& io = ImGui::GetIO();
    Vec3 lightDirection;
    ProductionPreviewSettings previewSettings = g_productionPreviewSettings;

    {
        std::lock_guard<std::mutex> lock(g_stateMutex);
        g_productionLightDirection = normalize(g_productionLightDirection);
        if (hovered && io.MouseWheel != 0.0f) {
            const float previousZoom = g_landscapeCamera.zoom;
            const float nextZoom = clampFloat(previousZoom * (1.0f + io.MouseWheel * 0.12f), 0.28f, 12.0f);
            if (nextZoom != previousZoom) {
                const ImVec2 anchor = meshProjectionAnchor(origin, viewportSize, g_landscapeCamera);
                const ImVec2 mouseLocal{
                    io.MousePos.x - anchor.x,
                    io.MousePos.y - anchor.y,
                };
                const float zoomRatio = nextZoom / previousZoom;
                g_landscapeCamera.pan.x += mouseLocal.x * (1.0f - zoomRatio);
                g_landscapeCamera.pan.y += mouseLocal.y * (1.0f - zoomRatio);
                g_landscapeCamera.zoom = nextZoom;
            }
        }

        if (dragging) {
            if (io.KeyCtrl) {
                g_productionLightDirection = rotateLightDirectionFromDrag(g_productionLightDirection, io.MouseDelta);
            } else {
                g_landscapeCamera.pan.x += io.MouseDelta.x;
                g_landscapeCamera.pan.y += io.MouseDelta.y;
            }
        }

        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            if (io.KeyCtrl) {
                g_productionLightDirection = defaultProductionLightDirection();
            } else {
                g_landscapeCamera.zoom = 1.0f;
                g_landscapeCamera.pan = {0.0f, 0.0f};
            }
        }
        lightDirection = g_productionLightDirection;
    }
    previewSettings = g_productionPreviewSettings;

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(18, 21, 27, 255));
    drawList->AddRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, IM_COL32(70, 78, 92, 255));
    drawList->PushClipRect(origin, {origin.x + viewportSize.x, origin.y + viewportSize.y}, true);

    bool renderedWithGpu = false;
    if (previewSettings.useGpuRenderer) {
        const std::vector<render_core::MeshPreviewQuad> renderQuads = buildRenderQuads(settings, model, previewSettings);
        renderedWithGpu = g_gpuPreviewRenderer.render(
            renderQuads,
            makeRenderParams(settings, g_landscapeCamera, viewportSize, lightDirection, previewSettings),
            g_textures.grass.view,
            g_textures.rock.view,
            g_textures.grass.sampler,
            (int)std::max(1.0f, viewportSize.x),
            (int)std::max(1.0f, viewportSize.y));
    }

    if (renderedWithGpu && g_gpuPreviewRenderer.validOutput()) {
        drawList->AddImage(
            textureId(g_gpuPreviewRenderer.outputView(), g_gpuPreviewRenderer.outputSampler()),
            origin,
            {origin.x + viewportSize.x, origin.y + viewportSize.y});
    } else {
        const ImVec2 groundCenter{origin.x + viewportSize.x * 0.5f, origin.y + 170.0f};
        drawList->AddEllipse(groundCenter, {390.0f, 150.0f}, IM_COL32(34, 39, 47, 255), 0.0f, 48, 2.0f);

        std::vector<int> drawOrder;
        drawOrder.reserve(model.meshQuads.size());
        for (int i = 0; i < (int)model.meshQuads.size(); i++) {
            const MeshQuad& quad = model.meshQuads[(std::size_t)i];
            if (quad.cliffWall && !settings.showCliffWalls) {
                continue;
            }
            if (!quad.cliffWall && !settings.showTopFaces) {
                continue;
            }
            drawOrder.push_back(i);
        }

        std::sort(drawOrder.begin(), drawOrder.end(), [&](int lhs, int rhs) {
            return model.meshQuads[(std::size_t)lhs].depth < model.meshQuads[(std::size_t)rhs].depth;
        });

        const ImDrawListFlags previousFlags = drawList->Flags;
        drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill;
        for (int index : drawOrder) {
            drawLandscapeProjectedQuad(
                drawList,
                settings,
                g_landscapeCamera,
                origin,
                viewportSize,
                model.meshQuads[(std::size_t)index],
                lightDirection,
                previewSettings);
        }
        drawList->Flags = previousFlags;
    }

    if (previewSettings.showEnvSprites && !g_envSprites.empty()) {
        if (g_envReseedRequested || g_envPlacements.empty()) {
            regenerateEnvPlacements(settings, model);
            g_envReseedRequested = false;
        }

        // Replicate the GPU isometric projection so overlaid sprites line up with
        // the rendered terrain image (the CPU fallback anchor differs slightly).
        const float anchorY = std::min(170.0f, std::max(96.0f, viewportSize.y * 0.34f));
        const float halfW = (float)settings.gridWidth * 0.5f;
        const float halfH = (float)settings.gridHeight * 0.5f;
        const float zoom = g_landscapeCamera.zoom;
        const ImVec2 pan = g_landscapeCamera.pan;
        auto projectWorld = [&](float wx, float wy, float wz) -> ImVec2 {
            const float cx = wx - halfW;
            const float cz = wz - halfH;
            const float sx = viewportSize.x * 0.5f + pan.x + (cx - cz) * 19.0f * zoom;
            const float sy = anchorY + pan.y + ((cx + cz) * 9.5f - wy * 25.0f) * zoom;
            return {origin.x + sx, origin.y + sy};
        };

        std::vector<int> order(g_envPlacements.size());
        for (int i = 0; i < (int)order.size(); i++) {
            order[i] = i;
        }
        std::sort(order.begin(), order.end(), [&](int lhs, int rhs) {
            const EnvPlacement& a = g_envPlacements[(std::size_t)lhs];
            const EnvPlacement& b = g_envPlacements[(std::size_t)rhs];
            return (a.x + a.z) < (b.x + b.z);
        });

        const float pxPerUnit = 25.0f * zoom;
        for (int index : order) {
            const EnvPlacement& placement = g_envPlacements[(std::size_t)index];
            if (placement.sprite < 0 || placement.sprite >= (int)g_envSprites.size()) {
                continue;
            }
            const EnvSprite& sprite = g_envSprites[(std::size_t)placement.sprite];
            if (!validTexture(sprite.tex)) {
                continue;
            }
            const ImVec2 base = projectWorld(placement.x, placement.y, placement.z);
            const float widthPx = sprite.worldWidth * placement.scale * pxPerUnit;
            const float heightPx = widthPx * (float)sprite.pixelHeight / (float)std::max(1, sprite.pixelWidth);
            const ImVec2 pMin{base.x - widthPx * 0.5f, base.y - heightPx};
            const ImVec2 pMax{base.x + widthPx * 0.5f, base.y};
            drawList->AddImage(textureId(sprite.tex), pMin, pMax);
        }
    }

    drawList->AddText({origin.x + 12.0f, origin.y + 12.0f}, IM_COL32(220, 228, 240, 255), "Production Preview: shaded landscape mesh");
    drawList->AddText({origin.x + 12.0f, origin.y + 32.0f}, IM_COL32(150, 162, 180, 255), "LMB drag: pan, Ctrl+LMB drag: rotate sun, mouse wheel: zoom");
    char cameraText[96];
    snprintf(cameraText, sizeof(cameraText), "Camera zoom: %.2fx, pan: %.0f %.0f", g_landscapeCamera.zoom, g_landscapeCamera.pan.x, g_landscapeCamera.pan.y);
    drawList->AddText({origin.x + 12.0f, origin.y + 52.0f}, IM_COL32(150, 162, 180, 255), cameraText);
    char lightText[128];
    snprintf(lightText, sizeof(lightText), "Sun dir: %.2f %.2f %.2f", lightDirection.x, lightDirection.y, lightDirection.z);
    drawList->AddText({origin.x + 12.0f, origin.y + 72.0f}, IM_COL32(150, 162, 180, 255), lightText);
    drawList->AddText(
        {origin.x + 12.0f, origin.y + 92.0f},
        renderedWithGpu ? IM_COL32(126, 220, 150, 255) : IM_COL32(235, 186, 90, 255),
        renderedWithGpu ? "Renderer: Sokol GPU offscreen" : "Renderer: ImGui CPU fallback");
    drawList->PopClipRect();
}
} // namespace meshgen_playground
