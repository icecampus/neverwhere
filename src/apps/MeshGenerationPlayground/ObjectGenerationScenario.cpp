#include "ObjectGenerationScenario.h"

#include "PlaygroundState.h"

#include <FastNoise/FastNoise.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

constexpr float kPi = 3.14159265358979323846f;

const std::array<ObjectGeneratorInfo, 1> kGeneratorInfos{{
    {
        ObjectGeneratorKind::CliffRock,
        "Cliff Rock",
        "FastNoise2 displaced rock pillar: ridged big forms plus fbm surface detail.",
    },
}};

Vec3 subtractVec(const Vec3& lhs, const Vec3& rhs) {
    return {lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z};
}

Vec3 crossVec(const Vec3& lhs, const Vec3& rhs) {
    return {
        lhs.y * rhs.z - lhs.z * rhs.y,
        lhs.z * rhs.x - lhs.x * rhs.z,
        lhs.x * rhs.y - lhs.y * rhs.x,
    };
}

Vec3 normalizeVec(const Vec3& value) {
    const float length = std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
    if (length <= 0.0001f) {
        return {0.0f, 1.0f, 0.0f};
    }
    return {value.x / length, value.y / length, value.z / length};
}

FastNoise::SmartNode<> makeFbmNode(int octaves, float lacunarity, float gain) {
    auto simplex = FastNoise::New<FastNoise::Simplex>();
    if (!simplex) {
        return nullptr;
    }
    auto fractal = FastNoise::New<FastNoise::FractalFBm>();
    if (!fractal) {
        return simplex;
    }
    fractal->SetSource(simplex);
    fractal->SetOctaveCount(octaves);
    fractal->SetLacunarity(lacunarity);
    fractal->SetGain(gain);
    return fractal;
}

// Low frequency form noise: drives the overall silhouette and the terraced strata.
FastNoise::SmartNode<> makeFormNoiseNode() {
    return makeFbmNode(4, 2.0f, 0.5f);
}

// Groove noise: Y-stretched fbm, ridged in code (1 - |n|) to carve sharp vertical
// fracture lines into the cliff walls. The FastNoise FractalRidged node returns a
// constant in this build, so we ridge the fbm output ourselves.
FastNoise::SmartNode<> makeCliffNoiseNode() {
    return makeFbmNode(3, 2.1f, 0.55f);
}

// Fine rocky surface: high frequency fbm fractal -> stone grain on top of the big forms.
FastNoise::SmartNode<> makeDetailNoiseNode() {
    return makeFbmNode(5, 2.4f, 0.5f);
}

void sampleNoiseBatch(
    const FastNoise::SmartNode<>& node,
    const std::vector<float>& xs,
    const std::vector<float>& ys,
    const std::vector<float>& zs,
    int seed,
    std::vector<float>& out) {

    const std::size_t count = xs.size();
    out.assign(count, 0.0f);
    if (count == 0 || !node) {
        return;
    }
    node->GenPositionArray3D(
        out.data(),
        (int)count,
        xs.data(),
        ys.data(),
        zs.data(),
        0.0f,
        0.0f,
        0.0f,
        seed);
    for (float& value : out) {
        value = clampFloat(value, -1.0f, 1.0f);
    }
}

ImU32 cliffColor(float dispNorm, float heightFraction, float crack) {
    // dispNorm: relative protrusion (>0 ridge, <0 shelf valley).
    const float ridge = clampFloat(0.5f + dispNorm * 0.5f, 0.0f, 1.0f);
    const float heightTint = clampFloat(heightFraction, 0.0f, 1.0f);

    // Base grey-brown stone, brighter on protruding ledges and slightly toward the top.
    const int base = 120 + (int)(ridge * 70.0f) + (int)(heightTint * 16.0f);
    const int warm = (int)((ridge - 0.5f) * 28.0f);
    int r = clampInt(base + warm, 70, 224);
    int g = clampInt(base + warm - 6, 66, 214);
    int b = clampInt(base - 8, 60, 200);

    // Darken fracture cracks toward cool shadow.
    if (crack > 0.0f) {
        const float c = clampFloat(crack, 0.0f, 1.0f);
        r = clampInt((int)((float)r * (1.0f - 0.6f * c)), 14, 182);
        g = clampInt((int)((float)g * (1.0f - 0.58f * c)), 14, 176);
        b = clampInt((int)((float)b * (1.0f - 0.5f * c)), 18, 168);
    }
    return IM_COL32(r, g, b, 255);
}

struct GridVertex {
    Vec3 position;
    float dispNorm = 0.0f;
    float heightFraction = 0.0f;
    float crack = 0.0f;
};

void addQuad(ObjectGenerationModel& model, const GridVertex& a, const GridVertex& b, const GridVertex& c, const GridVertex& d) {
    MeshQuad quad;
    quad.a = a.position;
    quad.b = b.position;
    quad.c = c.position;
    quad.d = d.position;
    quad.normal = normalizeVec(crossVec(subtractVec(b.position, a.position), subtractVec(c.position, a.position)));

    const float avgDisp = (a.dispNorm + b.dispNorm + c.dispNorm + d.dispNorm) * 0.25f;
    const float avgHeight = (a.heightFraction + b.heightFraction + c.heightFraction + d.heightFraction) * 0.25f;
    const float avgCrack = (a.crack + b.crack + c.crack + d.crack) * 0.25f;
    const bool recessed = avgCrack > 0.3f;
    quad.color = cliffColor(avgDisp, avgHeight, avgCrack);
    quad.cliffWall = recessed;
    quad.depth = meshQuadDepth(quad);

    model.meshQuads.push_back(quad);
    model.faceCount++;
    model.vertexCount += 4;
    if (recessed) {
        model.recessedQuadCount++;
    } else {
        model.rockQuadCount++;
    }
}

ObjectGenerationModel buildCliffRockModel(const ObjectGenerationSettings& settings) {
    ObjectGenerationModel model;

    const int rings = settings.rings;
    const int segments = settings.segments;
    const int ringVertexRows = rings + 1;
    const std::size_t gridCount = (std::size_t)ringVertexRows * (std::size_t)segments;
    const std::size_t pointCount = gridCount + 2; // + top/bottom cap centers.

    const float ry = settings.height * 0.5f;
    const float refRadius = (settings.radiusX + settings.radiusZ) * 0.5f;
    const float minRingRadius = 0.32f; // wide, broken top/bottom instead of a sharp spike

    // Base (pre-displacement) positions and outward normals for the whole grid plus caps.
    std::vector<Vec3> basePositions(pointCount);
    std::vector<Vec3> outwardDirs(pointCount);
    std::vector<float> heightFractions(pointCount, 0.0f);

    const float grooveScale = settings.cliffScale * 2.6f;

    std::vector<float> formX(pointCount), formY(pointCount), formZ(pointCount);
    std::vector<float> grooveX(pointCount), grooveY(pointCount), grooveZ(pointCount);
    std::vector<float> detailX(pointCount), detailY(pointCount), detailZ(pointCount);

    auto fillPoint = [&](std::size_t index, const Vec3& base, const Vec3& outward, float heightFraction) {
        basePositions[index] = base;
        outwardDirs[index] = outward;
        heightFractions[index] = heightFraction;
        // Form noise: low frequency, isotropic -> silhouette + strata.
        formX[index] = base.x * settings.cliffScale;
        formY[index] = base.y * settings.cliffScale;
        formZ[index] = base.z * settings.cliffScale;
        // Groove noise: higher frequency, compressed Y -> vertical fracture lines.
        grooveX[index] = base.x * grooveScale;
        grooveY[index] = base.y * grooveScale * settings.cliffYStretch;
        grooveZ[index] = base.z * grooveScale;
        // Detail noise: isotropic high frequency stone grain.
        detailX[index] = base.x * settings.detailScale;
        detailY[index] = base.y * settings.detailScale;
        detailZ[index] = base.z * settings.detailScale;
    };

    for (int r = 0; r < ringVertexRows; r++) {
        const float v = (float)r / (float)rings;
        const float theta = v * kPi;
        const float ringRadius = std::max(minRingRadius, std::sin(theta));
        const float yNorm = std::cos(theta); // +1 top, -1 bottom.
        const float heightFraction = (yNorm + 1.0f) * 0.5f;
        const float taperFactor = 1.0f - settings.taper * heightFraction;

        for (int s = 0; s < segments; s++) {
            const float phi = (2.0f * kPi * (float)s) / (float)segments;
            const float dirX = std::cos(phi);
            const float dirZ = std::sin(phi);

            Vec3 base;
            base.x = dirX * settings.radiusX * ringRadius * taperFactor;
            base.y = yNorm * ry;
            base.z = dirZ * settings.radiusZ * ringRadius * taperFactor;

            // Displacement direction: horizontal on the body (steep cliff walls), turning
            // vertical only near the poles so the caps still get broken up.
            const float vertW = clampFloat(1.0f - ringRadius * 1.4f, 0.0f, 1.0f);
            const float wall = 1.0f - vertW * settings.wallBias;
            const Vec3 ellipsoidNormal{
                base.x / (settings.radiusX * settings.radiusX + 0.0001f),
                base.y / (ry * ry + 0.0001f),
                base.z / (settings.radiusZ * settings.radiusZ + 0.0001f),
            };
            const Vec3 horizontalDir = normalizeVec({dirX, 0.0f, dirZ});
            const Vec3 outward = normalizeVec({
                horizontalDir.x * wall + ellipsoidNormal.x * (1.0f - wall),
                ellipsoidNormal.y * (1.0f - wall) + (yNorm >= 0.0f ? 1.0f : -1.0f) * vertW * settings.wallBias,
                horizontalDir.z * wall + ellipsoidNormal.z * (1.0f - wall),
            });

            fillPoint((std::size_t)r * (std::size_t)segments + (std::size_t)s, base, outward, heightFraction);
        }
    }

    const std::size_t topCenterIndex = gridCount;
    const std::size_t bottomCenterIndex = gridCount + 1;
    fillPoint(topCenterIndex, {0.0f, ry, 0.0f}, {0.0f, 1.0f, 0.0f}, 1.0f);
    fillPoint(bottomCenterIndex, {0.0f, -ry, 0.0f}, {0.0f, -1.0f, 0.0f}, 0.0f);

    const FastNoise::SmartNode<> formNode = makeFormNoiseNode();
    const FastNoise::SmartNode<> grooveNode = makeCliffNoiseNode();
    const FastNoise::SmartNode<> detailNode = makeDetailNoiseNode();
    model.usedFastNoise = static_cast<bool>(formNode);

    std::vector<float> formNoise;
    std::vector<float> grooveNoise;
    std::vector<float> detailNoise;
    sampleNoiseBatch(formNode, formX, formY, formZ, settings.seed, formNoise);
    sampleNoiseBatch(grooveNode, grooveX, grooveY, grooveZ, settings.seed + 707, grooveNoise);
    sampleNoiseBatch(detailNode, detailX, detailY, detailZ, settings.seed + 1311, detailNoise);

    // Center the form noise so it pushes out and pulls in symmetrically.
    float formMean = 0.0f;
    for (const float value : formNoise) {
        formMean += value;
    }
    if (!formNoise.empty()) {
        formMean /= (float)formNoise.size();
    }

    const float steps = (float)std::max(1, settings.cliffSteps);
    constexpr float kFormGain = 2.2f;

    // Per (band, angular sector) pseudo-random radial offset -> blocky faceted rock
    // layers (irregular polygonal cross-section) rather than smooth lathe discs.
    const int sectorCount = std::max(5, segments / 4);
    auto facetOffset = [&](int band, int sector) {
        std::uint32_t h = (std::uint32_t)band * 2654435761u
            + (std::uint32_t)sector * 2246822519u
            + (std::uint32_t)settings.seed * 40503u + 0x9e3779b9u;
        h ^= h >> 15;
        h *= 2246822519u;
        h ^= h >> 13;
        return ((float)(h & 0xffffu) / 65535.0f) * 2.0f - 1.0f;
    };

    // Displace every vertex outward along its (mostly horizontal) wall direction.
    std::vector<GridVertex> verts(pointCount);
    for (std::size_t i = 0; i < pointCount; i++) {
        // Lateral lumps from the form noise (breaks the round silhouette).
        const float formRaw = formNoise.empty() ? 0.0f : (formNoise[i] - formMean) * kFormGain;

        // Strata + facets: quantize height into wobbly bands and angle into sectors;
        // each (band, sector) cell juts out a different amount -> stacked eroded
        // ledges with vertical steps and irregular, broken outlines.
        const float bandF = heightFractions[i] * steps + formRaw * 0.55f;
        const int band = (int)std::floor(bandF);
        const float angle = std::atan2(basePositions[i].z, basePositions[i].x);
        const float sectorF = (angle / (2.0f * kPi) + 0.5f) * (float)sectorCount + formRaw * 0.5f;
        const int sector = (int)std::floor(sectorF);
        const float strata = facetOffset(band, sector);

        // Groove: fracture lines where the ridged noise peaks; sharpened but visible.
        const float ridge = grooveNoise.empty() ? 0.0f : (1.0f - std::fabs(grooveNoise[i]));
        float crack = clampFloat((ridge - 0.6f) / 0.4f, 0.0f, 1.0f);
        crack = crack * crack;

        const float detail = detailNoise.empty() ? 0.0f : detailNoise[i];

        const float dispNorm = clampFloat(
            strata * settings.terraceStrength
                + formRaw * settings.cliffStrength * 0.4f
                - crack * settings.grooveDepth
                + detail * settings.detailStrength,
            -1.0f, 1.0f);
        const float dispWorld = dispNorm * refRadius * 0.8f;

        GridVertex& vertex = verts[i];
        vertex.position = {
            basePositions[i].x + outwardDirs[i].x * dispWorld,
            basePositions[i].y + outwardDirs[i].y * dispWorld,
            basePositions[i].z + outwardDirs[i].z * dispWorld,
        };
        vertex.dispNorm = dispNorm;
        vertex.heightFraction = heightFractions[i];
        vertex.crack = crack;
    }

    model.gridVertexCount = (int)pointCount;
    model.meshQuads.reserve((std::size_t)rings * (std::size_t)segments + (std::size_t)segments);

    auto vertexAt = [&](int ring, int seg) -> const GridVertex& {
        const int wrapped = ((seg % segments) + segments) % segments;
        return verts[(std::size_t)ring * (std::size_t)segments + (std::size_t)wrapped];
    };

    // Side quads stitched across the whole grid.
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < segments; s++) {
            addQuad(model, vertexAt(r, s), vertexAt(r, s + 1), vertexAt(r + 1, s + 1), vertexAt(r + 1, s));
        }
    }

    // Top and bottom caps as quads (segments forced even), fanning two edges per quad.
    const GridVertex& topCenter = verts[topCenterIndex];
    const GridVertex& bottomCenter = verts[bottomCenterIndex];
    for (int s = 0; s < segments; s += 2) {
        addQuad(model, topCenter, vertexAt(0, s), vertexAt(0, s + 1), vertexAt(0, s + 2));
        addQuad(model, bottomCenter, vertexAt(rings, s + 2), vertexAt(rings, s + 1), vertexAt(rings, s));
    }

    return model;
}

} // namespace

int objectGeneratorCount() {
    return (int)kGeneratorInfos.size();
}

const ObjectGeneratorInfo& objectGeneratorInfo(int index) {
    index = clampInt(index, 0, objectGeneratorCount() - 1);
    return kGeneratorInfos[(std::size_t)index];
}

int objectGeneratorIndex(ObjectGeneratorKind kind) {
    for (int i = 0; i < objectGeneratorCount(); i++) {
        if (kGeneratorInfos[(std::size_t)i].kind == kind) {
            return i;
        }
    }
    return 0;
}

ObjectGeneratorKind objectGeneratorKindAt(int index) {
    return objectGeneratorInfo(index).kind;
}

const char* objectGeneratorName(ObjectGeneratorKind kind) {
    return objectGeneratorInfo(objectGeneratorIndex(kind)).name;
}

void sanitizeSettings(ObjectGenerationSettings& settings) {
    settings.generatorKind = objectGeneratorKindAt(objectGeneratorIndex(settings.generatorKind));
    settings.height = clampFloat(settings.height, 1.5f, 14.0f);
    settings.radiusX = clampFloat(settings.radiusX, 0.35f, 4.0f);
    settings.radiusZ = clampFloat(settings.radiusZ, 0.25f, 4.0f);
    settings.taper = clampFloat(settings.taper, 0.0f, 0.85f);
    settings.rings = clampInt(settings.rings, 6, 96);
    settings.segments = clampInt(settings.segments, 6, 96);
    if (settings.segments % 2 != 0) {
        settings.segments++;
    }
    settings.cliffScale = clampFloat(settings.cliffScale, 0.1f, 3.0f);
    settings.cliffStrength = clampFloat(settings.cliffStrength, 0.0f, 1.2f);
    settings.wallBias = clampFloat(settings.wallBias, 0.0f, 1.0f);
    settings.cliffSteps = clampInt(settings.cliffSteps, 2, 16);
    settings.terraceStrength = clampFloat(settings.terraceStrength, 0.0f, 1.0f);
    settings.cliffYStretch = clampFloat(settings.cliffYStretch, 0.05f, 1.5f);
    settings.grooveDepth = clampFloat(settings.grooveDepth, 0.0f, 1.0f);
    settings.detailScale = clampFloat(settings.detailScale, 0.5f, 12.0f);
    settings.detailStrength = clampFloat(settings.detailStrength, 0.0f, 0.6f);
}

void rebuildObjectGenerationModel() {
    spdlog::info("rebuildObjectGenerationModel: start");

    ObjectGenerationSettings settings = g_objectSettings;
    sanitizeSettings(settings);
    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_objectSettings = settings;
    }

    ObjectGenerationModel model;
    switch (settings.generatorKind) {
    case ObjectGeneratorKind::CliffRock:
    default:
        model = buildCliffRockModel(settings);
        break;
    }

    {
        std::lock_guard<std::mutex> lock(g_modelMutex);
        g_objectModel = std::move(model);
    }

    spdlog::info("rebuildObjectGenerationModel: done, generator={}, fastNoise={}, gridVerts={}, faces={}, rock/recessed={}/{}, vertices={}",
        objectGeneratorName(settings.generatorKind),
        g_objectModel.usedFastNoise,
        g_objectModel.gridVertexCount,
        g_objectModel.faceCount,
        g_objectModel.rockQuadCount,
        g_objectModel.recessedQuadCount,
        g_objectModel.vertexCount);
}

} // namespace meshgen_playground
