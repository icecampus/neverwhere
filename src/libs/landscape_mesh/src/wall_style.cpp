#include "landscape_mesh/wall_style.h"

#include <FastNoise/FastNoise.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace landscape_mesh {
namespace {

constexpr float kPi = 3.14159265358979323846f;

float clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

// Shared seam envelope. BOTH styles use this so the band pins identically at the
// top/bottom edges (and on the crest, heightT == 1 with fadeAtBottom == false
// yields 0). Keeping it shared is what guarantees watertight stitching.
float wallFade(float heightT, bool fadeAtBottom) {
    if (fadeAtBottom) {
        return std::sin(clamp01(heightT) * kPi);
    }
    float fade = 1.0f - clamp01(heightT);
    return fade * (0.35f + fade * 0.65f);
}

float terraceValue(float value, int steps) {
    if (steps <= 1) {
        return value;
    }
    const float normalized = clamp01((value + 1.0f) * 0.5f);
    const float terraced = std::floor(normalized * (float)steps) / (float)steps;
    return terraced * 2.0f - 1.0f;
}

// Analytic fallback rock when FastNoise is unavailable. Mirrors the legacy path so
// the block-cliff look is reproduced bit-for-bit when rockEnabled is off.
float deterministicRock(const MeshBuildSettings& settings, float x, float y, float z) {
    const float scaleValue = std::max(0.001f, settings.rockScale);
    const float seedOffset = (float)settings.rockSeed * 0.017f;
    const float sx = x * scaleValue + seedOffset;
    const float sy = y * scaleValue - seedOffset * 0.37f;
    const float sz = z * scaleValue + seedOffset * 0.61f;
    const float a = std::sin(sx * 19.17f + sy * 37.31f + sz * 11.73f) * 0.55f;
    const float b = std::sin(sx * 43.11f - sy * 17.27f + sz * 29.41f) * 0.30f;
    const float c = std::sin(sx * 83.63f + sy * 13.37f - sz * 47.09f) * 0.15f;
    return std::clamp(a + b + c, -1.0f, 1.0f);
}

ColorRgba blockWallColor(BoundarySide side, float heightT, float noiseValue) {
    const int sideBias = side == BoundarySide::Bottom ? 16 : (side == BoundarySide::Right ? -8 : 0);
    const int shade = (int)(noiseValue * 24.0f) + (int)((1.0f - heightT) * 12.0f) + sideBias;
    return {
        (std::uint8_t)std::clamp(92 + shade, 46, 170),
        (std::uint8_t)std::clamp(86 + shade, 44, 160),
        (std::uint8_t)std::clamp(78 + shade, 40, 150),
        255,
    };
}

ColorRgba rockTopColor(float noiseValue) {
    const int shade = (int)(noiseValue * 10.0f);
    return {
        (std::uint8_t)std::clamp(88 + shade, 58, 122),
        (std::uint8_t)std::clamp(143 + shade, 104, 172),
        (std::uint8_t)std::clamp(82 + shade, 52, 116),
        255,
    };
}

FastNoise::SmartNode<> makeRockNoiseNode(const MeshBuildSettings& settings) {
    auto simplex = FastNoise::New<FastNoise::Simplex>();
    if (!simplex) {
        return nullptr;
    }
    simplex->SetScale(settings.rockScale);

    auto fractal = FastNoise::New<FastNoise::FractalRidged>();
    if (!fractal) {
        return simplex;
    }
    fractal->SetSource(simplex);
    fractal->SetOctaveCount(4);
    fractal->SetLacunarity(2.15f);
    fractal->SetGain(0.55f);
    fractal->SetWeightedStrength(0.35f);
    return fractal;
}

void extractPositions(
    const std::vector<WallStyleSample>& samples,
    std::vector<float>& xs,
    std::vector<float>& ys,
    std::vector<float>& zs) {
    xs.resize(samples.size());
    ys.resize(samples.size());
    zs.resize(samples.size());
    for (std::size_t i = 0; i < samples.size(); i++) {
        xs[i] = samples[i].worldPos.x;
        ys[i] = samples[i].worldPos.y;
        zs[i] = samples[i].worldPos.z;
    }
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

// Reproduces the original block-cliff displacement (ridged simplex + terrace +
// seam fade) and colours. This is the default style; output is identical to the
// pre-refactor mesh.
class BlockCliffStyle : public IWallStyle {
public:
    void prepare(const WallStyleContext& ctx) override {
        m_settings = ctx.settings;
        m_node = m_settings.rockEnabled ? makeRockNoiseNode(m_settings) : nullptr;
    }

    void shade(const std::vector<WallStyleSample>& samples, std::vector<WallShade>& out) const override {
        out.assign(samples.size(), {});
        if (samples.empty()) {
            return;
        }

        std::vector<float> noise(samples.size(), 0.0f);
        if (m_node) {
            std::vector<float> xs;
            std::vector<float> ys;
            std::vector<float> zs;
            extractPositions(samples, xs, ys, zs);
            m_node->GenPositionArray3D(
                noise.data(),
                (int)samples.size(),
                xs.data(),
                ys.data(),
                zs.data(),
                0.0f,
                0.0f,
                0.0f,
                m_settings.rockSeed);
            for (float& value : noise) {
                value = std::clamp(value, -1.0f, 1.0f);
            }
        } else {
            for (std::size_t i = 0; i < samples.size(); i++) {
                noise[i] = deterministicRock(m_settings, samples[i].worldPos.x, samples[i].worldPos.y, samples[i].worldPos.z);
            }
        }

        const bool active = m_settings.rockEnabled && m_settings.rockAmplitude > 0.0f;
        const int steps = std::clamp(m_settings.terraceSteps, 0, 12);
        const float amp = std::max(0.0f, m_settings.rockAmplitude);
        for (std::size_t i = 0; i < samples.size(); i++) {
            const float field = noise[i];
            float offset = 0.0f;
            if (active) {
                const float stepped = terraceValue(field, steps);
                offset = stepped * amp * wallFade(samples[i].heightT, samples[i].fadeAtBottom) * samples[i].edgeWeight;
            }
            out[i] = {offset, field};
        }
    }

    ColorRgba color(WallPart part, BoundarySide side, float heightT, float panelField) const override {
        if (part == WallPart::Face) {
            return blockWallColor(side, heightT, panelField);
        }
        return rockTopColor(panelField);
    }

private:
    MeshBuildSettings m_settings;
    FastNoise::SmartNode<> m_node;
};

// Cyclopean masonry: blocky layered Perlin relief cut by Voronoi cell-edge
// grooves, so the wall reads as stacked irregular stones. Magnitude reuses
// rockAmplitude and the shared seam fade so it stays watertight and within the
// anti-fold clamp.
class CyclopeanStyle : public IWallStyle {
public:
    void prepare(const WallStyleContext& ctx) override {
        m_settings = ctx.settings;
        m_seed = ctx.seed;
        m_macro = makePerlinFractal(kMacroScale, 2);
        m_mid = makePerlinFractal(kMidScale, 4);
        m_layer = makePerlinFractal(kLayerScale, 1);
        m_crack = makeCellularEdge(kCellScale);
    }

    void shade(const std::vector<WallStyleSample>& samples, std::vector<WallShade>& out) const override {
        out.assign(samples.size(), {});
        if (samples.empty()) {
            return;
        }

        std::vector<float> xs;
        std::vector<float> ys;
        std::vector<float> zs;
        extractPositions(samples, xs, ys, zs);

        std::vector<float> macro;
        std::vector<float> mid;
        std::vector<float> layer;
        std::vector<float> crack;
        sampleNode(m_macro, xs, ys, zs, m_seed + 11, macro);
        sampleNode(m_mid, xs, ys, zs, m_seed + 23, mid);
        sampleNode(m_layer, xs, ys, zs, m_seed + 89, layer);
        sampleNode(m_crack, xs, ys, zs, m_seed + 101, crack);

        const bool active = m_settings.rockEnabled && m_settings.rockAmplitude > 0.0f;
        const float amp = std::max(0.0f, m_settings.rockAmplitude);
        for (std::size_t i = 0; i < samples.size(); i++) {
            const float blocky = toFactor(macro[i]) * 0.5f + toFactor(mid[i]) * 0.3f + toFactor(layer[i]) * 0.2f;
            const float relief = blocky * 2.0f - 1.0f;
            const float distEdge = std::fabs(crack[i]);
            const float crackMask = clamp01(1.0f - distEdge * kCrackEdgeScale);
            const float groove = crackMask * crackMask;
            const float field = std::clamp(relief - groove, -1.0f, 1.0f);
            float offset = 0.0f;
            if (active) {
                offset = field * amp * wallFade(samples[i].heightT, samples[i].fadeAtBottom) * samples[i].edgeWeight;
            }
            out[i] = {offset, field};
        }
    }

    ColorRgba color(WallPart part, BoundarySide side, float heightT, float panelField) const override {
        (void)side;
        (void)heightT;
        if (part == WallPart::Face) {
            return stoneColor(clamp01(panelField * 0.5f + 0.5f));
        }
        // Keep the walkable plateau ground-coloured; only the wall face turns to stone.
        return rockTopColor(panelField);
    }

private:
    static constexpr float kMacroScale = 13.0f;
    static constexpr float kMidScale = 3.4f;
    static constexpr float kLayerScale = 1.4f;
    static constexpr float kCellScale = 2.4f;
    static constexpr float kCrackEdgeScale = 3.0f;

    static float toFactor(float value) {
        return clamp01(value * 0.5f + 0.5f);
    }

    static FastNoise::SmartNode<> makePerlinFractal(float featureScale, int octaves) {
        auto perlin = FastNoise::New<FastNoise::Perlin>();
        if (!perlin) {
            return nullptr;
        }
        perlin->SetScale(featureScale);
        if (octaves <= 1) {
            return perlin;
        }
        auto fractal = FastNoise::New<FastNoise::FractalFBm>();
        if (!fractal) {
            return perlin;
        }
        fractal->SetSource(perlin);
        fractal->SetOctaveCount(octaves);
        fractal->SetGain(0.5f);
        fractal->SetLacunarity(2.0f);
        return fractal;
    }

    static FastNoise::SmartNode<> makeCellularEdge(float cellScale) {
        auto cellular = FastNoise::New<FastNoise::CellularDistance>();
        if (!cellular) {
            return nullptr;
        }
        cellular->SetScale(cellScale);
        cellular->SetDistanceFunction(FastNoise::DistanceFunction::Euclidean);
        cellular->SetReturnType(FastNoise::CellularDistance::ReturnType::Index0Sub1);
        cellular->SetDistanceIndex0(0);
        cellular->SetDistanceIndex1(1);
        cellular->SetGridJitter(1.0f);
        return cellular;
    }

    static ColorRgba stoneColor(float t) {
        t = clamp01(t);
        const auto mix = [&](int dark, int light) {
            return (std::uint8_t)std::clamp(
                (int)std::lround((float)dark + ((float)light - (float)dark) * t), 0, 255);
        };
        return {mix(58, 171), mix(56, 164), mix(52, 150), 255};
    }

    MeshBuildSettings m_settings;
    int m_seed = 0;
    FastNoise::SmartNode<> m_macro;
    FastNoise::SmartNode<> m_mid;
    FastNoise::SmartNode<> m_layer;
    FastNoise::SmartNode<> m_crack;
};

} // namespace

std::unique_ptr<IWallStyle> makeWallStyle(WallStyleId id) {
    switch (id) {
    case WallStyleId::Cyclopean:
        return std::make_unique<CyclopeanStyle>();
    case WallStyleId::BlockCliff:
    default:
        return std::make_unique<BlockCliffStyle>();
    }
}

} // namespace landscape_mesh
