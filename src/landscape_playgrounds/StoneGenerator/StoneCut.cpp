#include "pch.h"

#include "StoneCut.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace {

constexpr double kPi = 3.14159265358979323846;

// [0, 1) / [-1, 1) from the raw mt19937_64 output (STL-independent mapping).
double uni01(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0);
}
double uniSigned(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * (1.0 / 4503599627370496.0) - 1.0;
}

std::uint64_t mixSeed(int seed) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(seed)) + 0x9E3779B9ull) *
        0xBF58476D1CE4E5B9ull;
}

// Two perpendicular axes spanning the plane perpendicular to n.
void planeBasis(const glm::dvec3& n, glm::dvec3& u, glm::dvec3& v) {
    const glm::dvec3 ref = std::abs(n.y) < 0.9 ? glm::dvec3{0.0, 1.0, 0.0} : glm::dvec3{1.0, 0.0, 0.0};
    u = glm::normalize(glm::cross(n, ref));
    v = glm::cross(n, u);
}

} // namespace

void buildCutPoly(
    const StoneCutParams& params, StonePoly& outPoly, std::vector<StonePlane>& outPlanes) {

    std::mt19937_64 rng(mixSeed(params.seed));

    // The starting parallelepiped: base on y=0, centered on the XZ origin.
    const double sx = std::max(static_cast<double>(params.sizeX), 0.05);
    const double sz = std::max(static_cast<double>(params.sizeZ), 0.05);
    const double h = std::max(static_cast<double>(params.height), 0.05);
    std::vector<StonePlane> planes = {
        {{0.0, -1.0, 0.0}, 0.0, true}, // bottom (locked)
        {{0.0, 1.0, 0.0}, h, false},
        {{1.0, 0.0, 0.0}, sx, false},
        {{-1.0, 0.0, 0.0}, sx, false},
        {{0.0, 0.0, 1.0}, sz, false},
        {{0.0, 0.0, -1.0}, sz, false},
    };
    StonePoly poly = buildPolyhedron(planes);

    // Iterated corner cuts.
    const int cuts = std::clamp(params.cuts, 0, 64);
    const double tiltCone = std::tan(params.cutTiltDeg * kPi / 180.0);
    for (int cut = 0; cut < cuts; ++cut) {
        // Geometric center for the outward corner direction.
        glm::dvec3 center{0.0};
        for (const glm::dvec3& v : poly.verts) {
            center += v;
        }
        center /= static_cast<double>(poly.verts.size());

        // Candidate corners: everything except the base ring (keep the sit flat).
        const double baseCutY = 0.1 * h;
        std::vector<int> candidates;
        for (int i = 0; i < static_cast<int>(poly.verts.size()); ++i) {
            if (poly.verts[i].y > baseCutY) {
                candidates.push_back(i);
            }
        }
        if (candidates.empty()) {
            break;
        }
        const int corner = candidates[static_cast<std::size_t>(uni01(rng) * candidates.size()) %
                                      candidates.size()];
        const glm::dvec3 cv = poly.verts[corner];

        // Incident edge lengths scale the cut depth ("chip a little off").
        std::vector<double> edgeLens;
        for (const StonePoly::Face& face : poly.faces) {
            const int n = static_cast<int>(face.idx.size());
            for (int i = 0; i < n; ++i) {
                if (face.idx[i] == corner) {
                    edgeLens.push_back(glm::length(poly.verts[face.idx[(i + 1) % n]] - cv));
                    edgeLens.push_back(glm::length(poly.verts[face.idx[(i + n - 1) % n]] - cv));
                }
            }
        }
        std::sort(edgeLens.begin(), edgeLens.end());
        edgeLens.erase(std::unique(edgeLens.begin(), edgeLens.end()), edgeLens.end());
        double meanLen = 0.0;
        for (const double len : edgeLens) {
            meanLen += len;
        }
        meanLen /= std::max(edgeLens.size(), std::size_t{1});

        glm::dvec3 dir = cv - center;
        if (glm::length(dir) < 1e-9) {
            continue;
        }
        dir = glm::normalize(dir);
        glm::dvec3 u, v;
        planeBasis(dir, u, v);
        glm::dvec3 n = dir + tiltCone * (uniSigned(rng) * u + uniSigned(rng) * v);
        if (glm::length(n) < 1e-9) {
            continue;
        }
        n = glm::normalize(n);

        const double depth =
            params.cutDepth * meanLen * (0.6 + 0.8 * uni01(rng));
        const StonePlane cutPlane{n, glm::dot(n, cv) - depth, false};
        planes.push_back(cutPlane);
        clipByPlane(poly, cutPlane, static_cast<int>(planes.size()) - 1);
        if ((cut & 7) == 7) {
            compactPoly(poly); // keep the vertex array from drifting up
        }
    }
    compactPoly(poly);

    if (params.noiseAmp > 0.0f) {
        vertexNoise(poly, params.noiseAmp * std::max(sx, sz), mixSeed(params.seed) ^ 0x5F3759DFu);
    }

    const StonePlane ground{{0.0, -1.0, 0.0}, static_cast<double>(params.sink), true};
    planes.push_back(ground);
    clipByPlane(poly, ground, static_cast<int>(planes.size()) - 1);
    compactPoly(poly);

    outPoly = std::move(poly);
    outPlanes = std::move(planes);
}

StoneMesh generateCutStone(const StoneCutParams& params) {
    StonePoly poly;
    std::vector<StonePlane> planes;
    buildCutPoly(params, poly, planes);
    StoneMesh out;
    appendStoneMesh(out, poly, planes, params.seed, params.tintJitter);
    return out;
}
