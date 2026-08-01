#include "CliffWallScenario.h"

#include "PlaygroundState.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

#include <FastNoise/FastNoise.h>
#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

// Preview-only downscale: the Blender prototype is ~16x18 units, which does not
// fit inside the Quad Lab orbit camera (max ~12.9 unit distance). The cliff
// math runs in Blender world units; vertices are scaled by this when emitted so
// the look matches while the camera can frame the whole wall.
constexpr float kPreviewScale = 0.4f;

// Per-octave feature scales (1.0 / frequency) chosen to mimic the Blender Noise
// Texture "Scale" inputs on a ~16-unit-wide wall. Tunable look constants.
constexpr float kMacroScale = 13.0f;  // Blender Noise Texture.001 (scale 0.10, detail 2)
constexpr float kMidScale = 3.4f;     // Blender Noise Texture.002 (scale 0.55, detail 4)
constexpr float kMicroScale = 0.6f;   // Blender Noise Texture.003 (scale 5.0,  detail 6)
constexpr float kWarpScale = 5.0f;    // Blender Noise Texture.004 (scale 0.25, detail 2)
constexpr float kTopScale = 4.5f;     // Blender Noise Texture.005 (scale 0.30, detail 3)
constexpr float kLayerScale = 1.4f;   // Blender Noise Texture     (scale 2.70, detail 0)
constexpr float kCellScale = 2.4f;    // Blender Voronoi Texture   (scale 0.45, distance-to-edge)

// Blender per-field amplitude coefficients (Math.005..010 multipliers).
constexpr float kLayerCoeff = 0.55f;
constexpr float kMacroCoeff = 0.9f;
constexpr float kMidCoeff = 0.3f;
constexpr float kWarpCoeff = 1.6f;
constexpr float kLayerStepCoeff = 0.61f; // Blender Math.005 (floor(layer) * 0.61)
constexpr float kCrackEdgeScale = 3.0f;  // Blender Math.012 (distance * 6) analog for our edge metric

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

// FastNoise2 Perlin output is [-1, 1]; Blender's Noise Texture "Factor" is [0, 1].
float toFactor(float value) {
    return clamp01(value * 0.5f + 0.5f);
}

struct Vec3Math {
    static Vec3 sub(const Vec3& a, const Vec3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
    static Vec3 cross(const Vec3& a, const Vec3& b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    static float dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
};

FastNoise::SmartNode<> makePerlinFractal(float featureScale, int octaves) {
    auto perlin = FastNoise::New<FastNoise::Perlin>();
    perlin->SetScale(featureScale);
    if (octaves <= 1) {
        return perlin;
    }
    auto fractal = FastNoise::New<FastNoise::FractalFBm>();
    fractal->SetSource(perlin);
    fractal->SetOctaveCount(octaves);
    fractal->SetGain(0.5f);
    fractal->SetLacunarity(2.0f);
    return fractal;
}

FastNoise::SmartNode<> makeCellularEdge(float cellScale) {
    auto cellular = FastNoise::New<FastNoise::CellularDistance>();
    cellular->SetScale(cellScale);
    cellular->SetDistanceFunction(FastNoise::DistanceFunction::Euclidean);
    cellular->SetReturnType(FastNoise::CellularDistance::ReturnType::Index0Sub1);
    cellular->SetDistanceIndex0(0);
    cellular->SetDistanceIndex1(1);
    cellular->SetGridJitter(1.0f);
    return cellular;
}

void sampleNode(
    const FastNoise::SmartNode<>& node,
    const std::vector<float>& xs,
    const std::vector<float>& ys,
    const std::vector<float>& zs,
    int seed,
    std::vector<float>& out) {
    out.assign(xs.size(), 0.0f);
    if (xs.empty() || !node) {
        return;
    }
    node->GenPositionArray3D(
        out.data(),
        (int)xs.size(),
        xs.data(),
        ys.data(),
        zs.data(),
        0.0f,
        0.0f,
        0.0f,
        seed);
}

ImU32 stoneColor(float t) {
    t = clamp01(t);
    const auto mix = [&](int dark, int light) {
        return (int)std::lround((float)dark + ((float)light - (float)dark) * t);
    };
    // Recessed crevice -> protruding sunlit stone.
    return IM_COL32(mix(58, 171), mix(56, 164), mix(52, 150), 255);
}

} // namespace

void sanitizeCliffWallSettings(CliffWallSettings& settings) {
    settings.width = clampFloat(settings.width, 2.0f, 64.0f);
    settings.height = clampFloat(settings.height, 2.0f, 64.0f);
    settings.depth = clampFloat(settings.depth, 0.0f, 8.0f);
    settings.layers = clampInt(settings.layers, 1, 80);
    settings.topJag = clampFloat(settings.topJag, 0.0f, 8.0f);
    settings.crackDepth = clampFloat(settings.crackDepth, 0.0f, 3.0f);
    settings.micro = clampFloat(settings.micro, 0.0f, 1.0f);
    settings.resolutionX = clampInt(settings.resolutionX, 8, 320);
    settings.resolutionY = clampInt(settings.resolutionY, 8, 360);
}

void resetCliffWallCamera(QuadLabPreviewCamera& camera) {
    camera.zoom = 0.45f;
    camera.pan = {0.0f, 0.0f};
    camera.orbitYawDegrees = 180.0f;
    camera.orbitPitchDegrees = 6.0f;
}

void rebuildCliffWallModel() {
    CliffWallSettings settings = g_cliffWallSettings;
    sanitizeCliffWallSettings(settings);

    const int cols = settings.resolutionX + 1;
    const int rows = settings.resolutionY + 1;
    const std::size_t vertCount = (std::size_t)cols * (std::size_t)rows;

    const float halfWidth = settings.width * 0.5f;
    const float halfHeight = settings.height * 0.5f;
    const float invLayersHeight = settings.height > 0.0f ? 1.0f / settings.height : 0.0f;

    // Base sampling coordinates (Blender world units; wall centered on origin).
    std::vector<float> worldX(vertCount);
    std::vector<float> worldY(vertCount);
    std::vector<float> zeros(vertCount, 0.0f);
    std::vector<float> layerStep(vertCount);

    for (int row = 0; row < rows; row++) {
        const float v = rows > 1 ? (float)row / (float)(rows - 1) : 0.0f;
        const float y = -halfHeight + v * settings.height;       // [-H/2, H/2]
        const float h = clamp01((y + halfHeight) * invLayersHeight); // [0, 1] from base
        const int layerIndex = (int)std::floor(h * (float)settings.layers);
        const float step = (float)layerIndex * kLayerStepCoeff;
        for (int col = 0; col < cols; col++) {
            const float u = cols > 1 ? (float)col / (float)(cols - 1) : 0.0f;
            const std::size_t i = (std::size_t)row * (std::size_t)cols + (std::size_t)col;
            worldX[i] = -halfWidth + u * settings.width;          // [-W/2, W/2]
            worldY[i] = y;
            layerStep[i] = step;
        }
    }

    // Sample all noise fields in batches (Blender 4D W = Seed -> per-field seed).
    std::vector<float> macro;
    std::vector<float> mid;
    std::vector<float> micro;
    std::vector<float> warp;
    std::vector<float> top;
    std::vector<float> layer;
    std::vector<float> crackEdge;

    sampleNode(makePerlinFractal(kMacroScale, 2), worldX, zeros, worldY, settings.seed + 11, macro);
    sampleNode(makePerlinFractal(kMidScale, 4), worldX, zeros, worldY, settings.seed + 23, mid);
    sampleNode(makePerlinFractal(kMicroScale, 5), worldX, zeros, worldY, settings.seed + 41, micro);
    sampleNode(makePerlinFractal(kWarpScale, 2), worldX, zeros, worldY, settings.seed + 59, warp);
    sampleNode(makePerlinFractal(kTopScale, 3), worldX, zeros, zeros, settings.seed + 71, top);
    sampleNode(makePerlinFractal(kLayerScale, 1), zeros, zeros, layerStep, settings.seed + 89, layer);

    // Cracks: warp X horizontally, then sample a 1D-ish Voronoi edge field.
    std::vector<float> warpedX(vertCount);
    for (std::size_t i = 0; i < vertCount; i++) {
        warpedX[i] = worldX[i] + toFactor(warp[i]) * kWarpCoeff;
    }
    sampleNode(makeCellularEdge(kCellScale), warpedX, zeros, zeros, settings.seed + 101, crackEdge);

    // Resolve displacement per vertex.
    std::vector<Vec3> positions(vertCount);
    std::vector<float> depthAmounts(vertCount);
    float minDepth = std::numeric_limits<float>::max();
    float maxDepth = -std::numeric_limits<float>::max();

    for (std::size_t i = 0; i < vertCount; i++) {
        const float h = clamp01((worldY[i] + halfHeight) * invLayersHeight);

        const float layerOffset = toFactor(layer[i]) * kLayerCoeff;
        const float macroOffset = toFactor(macro[i]) * kMacroCoeff;
        const float midOffset = toFactor(mid[i]) * kMidCoeff;
        const float microOffset = toFactor(micro[i]) * settings.micro;

        const float distEdge = std::fabs(crackEdge[i]); // Index0Sub1 <= 0; magnitude grows into cells
        const float crackMask = clamp01(1.0f - distEdge * kCrackEdgeScale);
        const float crack = crackMask * crackMask * settings.crackDepth;

        const float depthSum = layerOffset + macroOffset + midOffset + microOffset + crack;
        const float depthAmount = depthSum * settings.depth;
        depthAmounts[i] = depthAmount;
        minDepth = std::min(minDepth, depthAmount);
        maxDepth = std::max(maxDepth, depthAmount);

        const float topMask = clamp01((h - 0.7f) / 0.3f);
        const float topAmount = topMask * toFactor(top[i]) * settings.topJag;

        positions[i] = {
            worldX[i] * kPreviewScale,
            (worldY[i] - topAmount) * kPreviewScale,
            -depthAmount * kPreviewScale,
        };
    }

    if (minDepth > maxDepth) {
        minDepth = 0.0f;
        maxDepth = 0.0f;
    }
    const float depthRange = std::max(0.0001f, maxDepth - minDepth);

    // Outward hint: the wall faces -Z (toward the default camera).
    const Vec3 outward{0.0f, 0.0f, -1.0f};

    CliffWallModel model;
    model.vertexCount = (int)vertCount;
    model.quads.reserve((std::size_t)settings.resolutionX * (std::size_t)settings.resolutionY);

    for (int row = 0; row < settings.resolutionY; row++) {
        for (int col = 0; col < settings.resolutionX; col++) {
            const std::size_t i00 = (std::size_t)row * (std::size_t)cols + (std::size_t)col;
            const std::size_t i10 = i00 + 1;
            const std::size_t i01 = i00 + (std::size_t)cols;
            const std::size_t i11 = i01 + 1;

            MeshQuad quad;
            quad.a = positions[i00];
            quad.b = positions[i10];
            quad.c = positions[i11];
            quad.d = positions[i01];

            // Reorder so (a, b, c) winds CCW as seen from outward (-Z): keeps the
            // face from being back-face culled and lets the renderer derive a
            // geometric normal for relief shading.
            Vec3 faceNormal = Vec3Math::cross(Vec3Math::sub(quad.b, quad.a), Vec3Math::sub(quad.c, quad.a));
            if (Vec3Math::dot(faceNormal, outward) < 0.0f) {
                std::swap(quad.b, quad.d);
            }
            quad.normal = outward;
            quad.outwardHint = outward;
            quad.cliffWall = false;

            const float avgDepth =
                (depthAmounts[i00] + depthAmounts[i10] + depthAmounts[i11] + depthAmounts[i01]) * 0.25f;
            quad.color = stoneColor((avgDepth - minDepth) / depthRange);
            quad.depth = meshQuadDepth(quad);

            model.quads.push_back(quad);
        }
    }

    model.quadCount = (int)model.quads.size();
    model.minDepth = minDepth;
    model.maxDepth = maxDepth;
    model.minHeight = -halfHeight * kPreviewScale;
    model.maxHeight = halfHeight * kPreviewScale;

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_cliffWallSettings = settings;
        g_cliffWallModel = std::move(model);
    }

    spdlog::info(
        "rebuildCliffWallModel: {:.1f}x{:.1f}, grid={}x{}, verts={}, quads={}, layers={}, depth={:.2f}, "
        "topJag={:.2f}, crack={:.2f}, micro={:.2f}, depthRange={:.2f}..{:.2f}",
        settings.width,
        settings.height,
        settings.resolutionX,
        settings.resolutionY,
        g_cliffWallModel.vertexCount,
        g_cliffWallModel.quadCount,
        settings.layers,
        settings.depth,
        settings.topJag,
        settings.crackDepth,
        settings.micro,
        g_cliffWallModel.minDepth,
        g_cliffWallModel.maxDepth);
}

bool runCliffWallSmokeTest() {
    const CliffWallSettings previousSettings = g_cliffWallSettings;

    CliffWallSettings testSettings;
    testSettings.resolutionX = 48;
    testSettings.resolutionY = 56;
    g_cliffWallSettings = testSettings;
    rebuildCliffWallModel();

    const int expectedQuads = testSettings.resolutionX * testSettings.resolutionY;
    const int actualQuads = g_cliffWallModel.quadCount;
    const bool reliefPresent = (g_cliffWallModel.maxDepth - g_cliffWallModel.minDepth) > 0.01f;
    const bool ok = actualQuads == expectedQuads && g_cliffWallModel.vertexCount > 0 && reliefPresent;

    g_cliffWallSettings = previousSettings;
    rebuildCliffWallModel();

    spdlog::info(
        "{} cliff wall FastNoise2 port: quads={} (expected {}), reliefRange={:.3f}",
        ok ? "TEST PASS" : "TEST FAIL",
        actualQuads,
        expectedQuads,
        g_cliffWallModel.maxDepth - g_cliffWallModel.minDepth);
    return ok;
}

} // namespace meshgen_playground
