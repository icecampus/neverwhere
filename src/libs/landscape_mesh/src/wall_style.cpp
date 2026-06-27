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

// Cyclopean masonry: irregular polygonal stones (Voronoi cells) raised into
// FLAT-topped blocks, separated by thin recessed grooves, with subtle per-course
// banding and a slow whole-wall tilt. Matched to the Blender cyclopean mesh, which
// is discrete beveled blocks with hardened normals -- so here each cell gets one
// constant height (no within-stone perlin lumpiness) and the mortar line is a
// narrow cubic recess. Feature sizes are in WORLD units sized for ~1-unit walls,
// and the cells are sampled in full 3D so stones stay coherent across adjacent
// boundary segments. Magnitude reuses rockAmplitude and the shared seam fade so the
// band stays watertight and within the anti-fold clamp.
class CyclopeanStyle : public IWallStyle {
public:
    void prepare(const WallStyleContext& ctx) override {
        m_settings = ctx.settings;
        m_seed = ctx.seed;
        m_macro = makePerlinFractal(kMacroScale, 2);
        m_warpA = makePerlinFractal(kWarpScale, 2);
        m_warpB = makePerlinFractal(kWarpScale, 2);
        m_layer = makePerlinFractal(kLayerNoiseScale, 1);
        m_crack = makeCellularEdge(kCellScale);
        m_cellValue = makeCellularValue(kCellScale);
    }

    void shade(const std::vector<WallStyleSample>& samples, std::vector<WallShade>& out) const override {
        out.assign(samples.size(), {});
        if (samples.empty()) {
            return;
        }
        const std::size_t count = samples.size();

        std::vector<float> xs;
        std::vector<float> ys;
        std::vector<float> zs;
        extractPositions(samples, xs, ys, zs);

        std::vector<float> macro;
        std::vector<float> warpA;
        std::vector<float> warpB;
        sampleNode(m_macro, xs, ys, zs, m_seed + 11, macro);
        sampleNode(m_warpA, xs, ys, zs, m_seed + 59, warpA);
        sampleNode(m_warpB, xs, ys, zs, m_seed + 60, warpB);

        // Domain-warp the cell sample positions so stone outlines look hand-laid
        // instead of gridded. The warp is a pure function of world position, so
        // shared seam vertices warp identically and the band stays watertight.
        std::vector<float> cellX(count);
        std::vector<float> cellZ(count);
        for (std::size_t i = 0; i < count; i++) {
            cellX[i] = xs[i] + (toFactor(warpA[i]) - 0.5f) * 2.0f * kWarpAmount;
            cellZ[i] = zs[i] + (toFactor(warpB[i]) - 0.5f) * 2.0f * kWarpAmount;
        }

        // Distance-to-edge metric and per-cell value share the same cell layout
        // (identical scale/jitter/seed), so each stone gets one coherent height.
        std::vector<float> crack;
        std::vector<float> cellValue;
        sampleNode(m_crack, cellX, ys, cellZ, m_seed + 101, crack);
        sampleNode(m_cellValue, cellX, ys, cellZ, m_seed + 101, cellValue);

        // Per-course banding: quantise world height into masonry rows and jitter
        // each row by sampling a value field at the quantised step coordinate.
        std::vector<float> zeros(count, 0.0f);
        std::vector<float> layerZ(count);
        for (std::size_t i = 0; i < count; i++) {
            layerZ[i] = std::floor(ys[i] * kCoursesPerUnit) * kLayerStepCoeff;
        }
        std::vector<float> layer;
        sampleNode(m_layer, zeros, zeros, layerZ, m_seed + 89, layer);

        const bool active = m_settings.rockEnabled && m_settings.rockAmplitude > 0.0f;
        const float amp = std::max(0.0f, m_settings.rockAmplitude);
        for (std::size_t i = 0; i < count; i++) {
            // Each stone is a FLAT-topped block. Its top sits proud of the seam
            // plane (kStoneBase); CellularValue gives one constant height per cell,
            // so the interior of a stone stays flat instead of perlin-lumpy. A slow
            // macro tilt (feature size >> stone) and a per-course bias only nudge the
            // whole block up/down -- they do not ripple within it. This mirrors the
            // Blender cyclopean mesh: discrete beveled blocks, not a noisy heightfield.
            const float stoneTop =
                kStoneBase +
                (toFactor(cellValue[i]) - 0.5f) * kCellHeightCoeff +
                (toFactor(macro[i]) - 0.5f) * kMacroCoeff +
                (toFactor(layer[i]) - 0.5f) * kCourseCoeff;

            // FastNoise Index0Sub1 is measured to peak (-crack ~1.0) ALONG the cell seams
            // and fall to ~0.09 at the cell centres -- i.e. -crack is "seam proximity",
            // bright thin lines exactly on the Voronoi edges. So the groove follows HIGH
            // seam proximity: ramp from the flat stone face up into a fixed dark mortar
            // floor across [kEdgeLo, kEdgeHi]. The fixed floor makes every seam a
            // continuous recessed mortar line regardless of the two stones' heights --
            // the crisp dark outline the Blender cyclopean mesh gets from its bevel + gaps.
            const float seam = -crack[i];
            const float t = clamp01((seam - kEdgeLo) / (kEdgeHi - kEdgeLo));
            const float groove = t * t * (3.0f - 2.0f * t);
            const float field = std::clamp(stoneTop + (kGrooveFloor - stoneTop) * groove, -1.0f, 1.0f);

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
    // Feature sizes are WORLD units (FastNoise SetScale == feature size; larger =
    // bigger). Measured against the Blender cyclopean mesh: stones are ~0.5 units
    // across, which yields a couple of chunky courses across a ~1-unit landscape
    // wall (genuinely "cyclopean" -- big irregular blocks).
    static constexpr float kCellScale = 0.5f;        // stone size (Voronoi cell ~0.5 units, matches Blender 0.56)
    static constexpr float kWarpScale = 1.4f;        // domain-warp feature size for organic edges
    static constexpr float kMacroScale = 4.0f;       // slow undulation over many stones (>> stone, stays flat per block)
    static constexpr float kLayerNoiseScale = 0.9f;  // per-course value field

    static constexpr float kWarpAmount = 0.18f;      // world-space crack waver (~third of a cell)
    static constexpr float kCoursesPerUnit = 2.2f;   // masonry rows per world unit of height
    static constexpr float kLayerStepCoeff = 0.6f;   // per-course step (Blender Math.005)

    // Mortar geometry, calibrated against the raw -crack ("seam proximity") dump: ~1.0
    // on the Voronoi edges, ~0.09 at cell centres. Below kEdgeLo is flat stone face;
    // [kEdgeLo..kEdgeHi] bevels down into the dark mortar floor at the seam. Tuned so
    // the line is thick enough to survive the landscape's per-quad averaging yet still
    // reads as crisp masonry.
    static constexpr float kEdgeLo = 0.66f;          // seam proximity where the bevel starts
    static constexpr float kEdgeHi = 0.93f;          // seam proximity at the bottom of the groove
    static constexpr float kGrooveFloor = -0.85f;    // dark recess the seam settles into

    // Stones bulge proud of the plane, grooves recess below it.
    static constexpr float kStoneBase = 0.5f;        // baseline lift so faces read as raised/bright
    static constexpr float kCellHeightCoeff = 0.55f; // distinct per-stone height + colour spread
    static constexpr float kMacroCoeff = 0.14f;      // gentle whole-wall tilt
    static constexpr float kCourseCoeff = 0.16f;     // per-course up/down bias

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

    // Same partition as makeCellularEdge (matching scale/jitter/distance/seed),
    // so the returned per-cell white-noise value lines up with the groove field.
    static FastNoise::SmartNode<> makeCellularValue(float cellScale) {
        auto cellular = FastNoise::New<FastNoise::CellularValue>();
        if (!cellular) {
            return nullptr;
        }
        cellular->SetScale(cellScale);
        cellular->SetDistanceFunction(FastNoise::DistanceFunction::Euclidean);
        cellular->SetValueIndex(0);
        cellular->SetGridJitter(1.0f);
        return cellular;
    }

    static ColorRgba stoneColor(float t) {
        t = clamp01(t);
        const auto mix = [&](int dark, int light) {
            return (std::uint8_t)std::clamp(
                (int)std::lround((float)dark + ((float)light - (float)dark) * t), 0, 255);
        };
        // Recessed groove (deep cool brown) -> raised sunlit stone (warm sandstone).
        // Wide range so the masonry still reads after the preview's wall-colour mute.
        return {mix(58, 224), mix(50, 202), mix(40, 166), 255};
    }

    MeshBuildSettings m_settings;
    int m_seed = 0;
    FastNoise::SmartNode<> m_macro;
    FastNoise::SmartNode<> m_warpA;
    FastNoise::SmartNode<> m_warpB;
    FastNoise::SmartNode<> m_layer;
    FastNoise::SmartNode<> m_crack;
    FastNoise::SmartNode<> m_cellValue;
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
