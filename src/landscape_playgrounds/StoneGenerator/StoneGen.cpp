#include "pch.h"

#include "StoneGen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

#include <glm/gtc/matrix_transform.hpp>

namespace {

constexpr double kPi = 3.14159265358979323846;

double degToRad(double deg) {
    return deg * kPi / 180.0;
}

std::uint64_t seedFromParams(int seed) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(seed)) + 0x9E3779B9ull) *
        0xBF58476D1CE4E5B9ull;
}

// [-1, 1) from the raw mt19937_64 output (uniform_real_distribution maps
// differently across STL implementations, breaking cross-platform seeds).
double uniSigned(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * (1.0 / 4503599627370496.0) - 1.0;
}

// [0, 1), same mapping.
double uni01(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0);
}

// [0,1) hash per (seed, planeId) — the per-face tint variation.
float faceTintHash(std::uint64_t seed, int planeId) {
    std::uint64_t z = seed + 0x9E3779B97F4A7C15ull * (static_cast<std::uint64_t>(planeId) + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return static_cast<float>(static_cast<double>(z >> 11) * (1.0 / 9007199254740992.0));
}

bool isPrismFamily(StoneBaseForm form) {
    return form == StoneBaseForm::Box || form == StoneBaseForm::Frustum ||
        form == StoneBaseForm::Prism;
}

} // namespace

void buildStonePoly(
    const StoneGenParams& params, StonePoly& outPoly, std::vector<StonePlane>& outPlanes) {

    std::mt19937_64 rng(seedFromParams(params.seed));
    const double radius = std::max(static_cast<double>(params.radius), 0.05);
    const double variance = std::clamp(static_cast<double>(params.shapeVariance), 0.0, 1.0);

    // Seeded shape variance around the slider values, drawn in a fixed order:
    // proportions nudges first (they feed the factories), elongation last.
    const double heightScale = 1.0 + variance * 0.20 * uniSigned(rng);
    const double taperNudge = variance * 0.12 * uniSigned(rng);
    const double yawExtraDeg = variance * 180.0 * uniSigned(rng);
    const double elongX = 1.0 + variance * 0.25 * uniSigned(rng);
    const double elongZ = 1.0 + variance * 0.25 * uniSigned(rng);

    std::vector<StonePlane> planes;
    if (isPrismFamily(params.form)) {
        const int sides = params.form == StoneBaseForm::Prism ? std::max(params.sides, 3) : 4;
        const double taper =
            params.form == StoneBaseForm::Box ? 0.0 : static_cast<double>(params.taper);
        planes = makePrismPlanes(
            sides,
            radius,
            std::max(static_cast<double>(params.height) * heightScale, 0.05),
            std::clamp(taper + taperNudge, 0.0, 0.98),
            degToRad(params.yawDeg + yawExtraDeg));
    } else {
        planes = makeBallPlanes(
            std::max(params.ballPlanes, 4), radius, params.planeOffset, rng);
    }

    jitterPlanes(
        planes,
        degToRad(params.planeTiltDeg),
        params.planeOffset,
        radius,
        rng);

    StonePoly poly = buildPolyhedron(planes);

    if (params.chamferWidth > 0.0f) {
        const std::vector<StonePlane> chamfers = chamferPlanes(
            poly, planes, params.chamferWidth, params.chamferTopOnly);
        for (const StonePlane& plane : chamfers) {
            planes.push_back(plane);
            clipByPlane(poly, plane, static_cast<int>(planes.size()) - 1);
        }
        compactPoly(poly);
    }

    if (params.noiseAmp > 0.0f) {
        vertexNoise(poly, params.noiseAmp * radius, seedFromParams(params.seed) ^ 0x5F3759DFu);
    }

    // Anisotropic scale: seeded elongation for every form, plus the explicit
    // Oval factors.
    const double scaleX = elongX * (params.form == StoneBaseForm::Oval ? params.ovalScaleX : 1.0);
    const double scaleZ = elongZ * (params.form == StoneBaseForm::Oval ? params.ovalScaleZ : 1.0);
    if (scaleX != 1.0 || scaleZ != 1.0) {
        const glm::dmat4 scale(
            scaleX, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, scaleZ, 0.0,
            0.0, 0.0, 0.0, 1.0);
        transformPoly(poly, scale);
        transformPlanes(planes, scale);
    }

    if (!isPrismFamily(params.form)) {
        // Balls are built around the origin: lift so most of the rock is above
        // ground, the ground clip then cuts a flat base.
        const glm::dmat4 lift(
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.72 * radius, 0.0, 1.0);
        transformPoly(poly, lift);
        transformPlanes(planes, lift);
    }

    const StonePlane ground{{0.0, -1.0, 0.0}, static_cast<double>(params.sink), true};
    planes.push_back(ground);
    clipByPlane(poly, ground, static_cast<int>(planes.size()) - 1);
    compactPoly(poly);

    outPoly = std::move(poly);
    outPlanes = std::move(planes);
}

void appendStoneMesh(
    StoneMesh& out,
    const StonePoly& poly,
    const std::vector<StonePlane>& planes,
    int seed,
    float tintJitter) {

    // Family light: same fixed sun + hemisphere as fence_core bakes.
    const glm::dvec3 sun = glm::normalize(glm::dvec3{-0.55, 0.80, -0.35});
    const glm::vec3 albedo{0.74f, 0.68f, 0.57f}; // warm beige, tmp/rock_example
    const std::uint64_t seedBits = seedFromParams(seed);

    glm::dvec3 emin{1e30}, emax{-1e30};
    if (!out.pos.empty()) {
        emin = glm::dvec3(out.extentMin);
        emax = glm::dvec3(out.extentMax);
    }
    int addedVerts = 0;

    for (const StonePoly::Face& face : poly.faces) {
        glm::dvec3 n = faceNormal(poly, face);
        // The generating plane knows the true outward direction — flip the
        // Newell normal if the winding ended up facing in.
        if (face.planeId >= 0 && face.planeId < static_cast<int>(planes.size()) &&
            glm::dot(n, planes[face.planeId].n) < 0.0) {
            n = -n;
        }

        const double diff = std::max(glm::dot(n, sun), 0.0);
        const double up = n.y * 0.5 + 0.5;
        const double light = std::min(0.40 + 0.30 * up + 0.55 * diff, 1.25);
        const double tint =
            1.0 + tintJitter * (static_cast<double>(faceTintHash(seedBits, face.planeId)) - 0.5) * 2.0;
        const glm::dvec3 rgb = glm::dvec3(albedo) * (light * tint);

        const int count = static_cast<int>(face.idx.size());
        for (int i = 1; i + 1 < count; ++i) {
            const int tri[3] = {face.idx[0], face.idx[i], face.idx[i + 1]};
            for (const int vi : tri) {
                const glm::dvec3& v = poly.verts[vi];
                out.pos.insert(out.pos.end(), {
                    static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)});
                out.nrm.insert(out.nrm.end(), {
                    static_cast<float>(n.x), static_cast<float>(n.y), static_cast<float>(n.z)});
                out.col.insert(out.col.end(), {
                    static_cast<float>(rgb.x), static_cast<float>(rgb.y), static_cast<float>(rgb.z), 1.0f});
                emin = glm::min(emin, v);
                emax = glm::max(emax, v);
                ++addedVerts;
            }
        }
    }
    out.triCount += addedVerts / 3;
    out.extentMin = glm::vec3(emin);
    out.extentMax = glm::vec3(emax);
}

StoneMesh generateLobe(const StoneGenParams& params) {
    StoneMesh out;

    StonePoly poly;
    std::vector<StonePlane> planes;
    buildStonePoly(params, poly, planes);
    appendStoneMesh(out, poly, planes, params.seed, params.tintJitter);

    const int blocks = std::clamp(params.blocks, 1, 3);
    if (blocks < 2) {
        return out;
    }

    // Placement draws for the extra blocks (own stream, fixed order).
    std::mt19937_64 lobeRng(seedFromParams(params.seed) ^ 0x9E3779B97F4A7C15ull);
    const glm::dvec3 baseExtentMax(out.extentMax);

    // Block 1: smaller block stacked on top, shifted and rotated.
    {
        StoneGenParams top = params;
        top.seed = params.seed + 101;
        top.radius = params.radius * (0.55f + 0.15f * static_cast<float>(uni01(lobeRng)));
        top.height = params.height * (0.65f + 0.20f * static_cast<float>(uni01(lobeRng)));
        top.yawDeg += 30.0f + 30.0f * static_cast<float>(uni01(lobeRng));
        top.blocks = 1;
        const double dx = params.radius * 0.30 * uniSigned(lobeRng);
        const double dz = params.radius * 0.30 * uniSigned(lobeRng);
        const double baseY = baseExtentMax.y * (0.55 + 0.15 * uni01(lobeRng));

        StonePoly topPoly;
        std::vector<StonePlane> topPlanes;
        buildStonePoly(top, topPoly, topPlanes);
        glm::dmat4 m(1.0);
        m[3] = glm::dvec4(dx, baseY, dz, 1.0);
        transformPoly(topPoly, m);
        transformPlanes(topPlanes, m);
        appendStoneMesh(out, topPoly, topPlanes, top.seed, top.tintJitter);
    }

    // Block 2: side block leaning against the base rock.
    if (blocks > 2) {
        StoneGenParams side = params;
        side.seed = params.seed + 202;
        side.radius = params.radius * (0.40f + 0.15f * static_cast<float>(uni01(lobeRng)));
        side.height = params.height * (0.50f + 0.20f * static_cast<float>(uni01(lobeRng)));
        side.blocks = 1;
        const double ang = 2.0 * kPi * uni01(lobeRng);
        const double lean = degToRad(6.0 + 8.0 * uni01(lobeRng));
        const double dist = params.radius * (0.75 + 0.15 * uni01(lobeRng));

        StonePoly sidePoly;
        std::vector<StonePlane> sidePlanes;
        buildStonePoly(side, sidePoly, sidePlanes);
        // Lean around the axis tangential to the radial direction, then move out.
        const glm::dvec3 axis{std::cos(ang + kPi * 0.5), 0.0, std::sin(ang + kPi * 0.5)};
        const glm::dmat4 rot = glm::rotate(glm::dmat4(1.0), lean, axis);
        glm::dmat4 m(1.0);
        m[3] = glm::dvec4(std::cos(ang) * dist, 0.0, std::sin(ang) * dist, 1.0);
        const glm::dmat4 full = m * rot;
        transformPoly(sidePoly, full);
        transformPlanes(sidePlanes, full);
        appendStoneMesh(out, sidePoly, sidePlanes, side.seed, side.tintJitter);
    }

    return out;
}

StoneMesh generateStone(const StoneGenParams& params) {
    return generateLobe(params);
}
