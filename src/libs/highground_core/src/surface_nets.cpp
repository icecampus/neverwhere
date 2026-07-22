#include "pch.h"

#include "highground_core/surface_nets.h"

#include <chrono>
#include <unordered_map>

namespace cliff {

namespace {

// Voxel corners, index bits: 1 = +x, 2 = +y, 4 = +z.
const int kCorner[8][3] = {
    {0, 0, 0}, {1, 0, 0}, {0, 1, 0}, {1, 1, 0},
    {0, 0, 1}, {1, 0, 1}, {0, 1, 1}, {1, 1, 1},
};

// 12 voxel edges as corner index pairs.
const int kEdge[12][2] = {
    {0, 1}, {1, 3}, {3, 2}, {2, 0},
    {4, 5}, {5, 7}, {7, 6}, {6, 4},
    {0, 4}, {1, 5}, {3, 7}, {2, 6},
};

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0, Clock::time_point t1) {
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

// glm::normalize has no zero-length fallback; a degenerate gradient (flat
// field region) must not produce NaN normals.
glm::vec3 safeNormalize(const glm::vec3& v) {
    const float len2 = glm::dot(v, v);
    return len2 > 1e-12f ? v * (1.0f / std::sqrt(len2)) : glm::vec3(0.0f, 1.0f, 0.0f);
}

} // namespace

void regularizeSigns(const CliffField& field, std::vector<float>& samples,
    RegularizeStats* stats) {
    const int nx = field.sizeX();
    const int ny = field.sizeY();
    const int nz = field.sizeZ();
    const int px = nx + 1;
    const int pz = nz + 1;
    const float cell = field.params().cellSize;
    const glm::vec3 org = field.origin();
    const int dims[3] = {nx, ny, nz};

    auto valueAt = [&](int x, int y, int z) -> float& {
        return samples[(static_cast<size_t>(y) * pz + z) * px + x];
    };

    // Face corners around axis d: base, +u, +u+v, +v (cycle order).
    auto faceCorners = [&](const int base[3], int d, int out[4][3]) {
        const int u = (d + 1) % 3;
        const int v = (d + 2) % 3;
        for (int k = 0; k < 4; ++k) {
            out[k][0] = base[0];
            out[k][1] = base[1];
            out[k][2] = base[2];
        }
        out[1][u] += 1;
        out[2][u] += 1;
        out[2][v] += 1;
        out[3][v] += 1;
    };
    auto faceIsSaddle = [&](const int base[3], int d, float vals[4], bool neg[4]) {
        int c[4][3];
        faceCorners(base, d, c);
        for (int k = 0; k < 4; ++k) {
            vals[k] = valueAt(c[k][0], c[k][1], c[k][2]);
            neg[k] = vals[k] < 0.0f;
        }
        return neg[0] == neg[2] && neg[1] == neg[3] && neg[0] != neg[1];
    };
    // The 12 grid faces touching a grid point (4 per axis orientation).
    auto touchingSaddles = [&](const int p[3]) {
        int count = 0;
        for (int d = 0; d < 3; ++d) {
            const int u = (d + 1) % 3;
            const int v = (d + 2) % 3;
            for (int du = -1; du <= 0; ++du) {
                for (int dv = -1; dv <= 0; ++dv) {
                    int base[3] = {p[0], p[1], p[2]};
                    base[u] += du;
                    base[v] += dv;
                    if (base[u] < 0 || base[v] < 0 || base[u] >= dims[u] || base[v] >= dims[v]) {
                        continue;
                    }
                    float vals[4];
                    bool neg[4];
                    if (faceIsSaddle(base, d, vals, neg)) {
                        ++count;
                    }
                }
            }
        }
        return count;
    };

    RegularizeStats localStats;
    RegularizeStats& out = stats != nullptr ? *stats : localStats;
    out = RegularizeStats{};

    // Flip the weakest corner of the sacrificed diagonal on every checkerboard
    // face. Between the two candidate corners prefer the flip that creates no
    // new saddles on the 12 faces touching the corner (local optimality keeps
    // the iteration from cascading).
    for (int pass = 0; pass < 16; ++pass) {
        int passSaddles = 0;
        int passFlips = 0;
        for (int d = 0; d < 3; ++d) {
            const int u = (d + 1) % 3;
            const int v = (d + 2) % 3;
            int base[3];
            for (base[2] = 0; base[2] <= dims[2]; ++base[2]) {
                for (base[1] = 0; base[1] <= dims[1]; ++base[1]) {
                    for (base[0] = 0; base[0] <= dims[0]; ++base[0]) {
                        if (base[u] >= dims[u] || base[v] >= dims[v]) {
                            continue;
                        }
                        float vals[4];
                        bool neg[4];
                        if (!faceIsSaddle(base, d, vals, neg)) {
                            continue;
                        }
                        ++passSaddles;
                        // The diagonal whose sign matches the face center stays
                        // connected; the other one loses its weakest corner.
                        const glm::vec3 center(
                            org.x + cell * (static_cast<float>(base[0]) + (d == 0 ? 0.0f : 0.5f)),
                            org.y + cell * (static_cast<float>(base[1]) + (d == 1 ? 0.0f : 0.5f)),
                            org.z + cell * (static_cast<float>(base[2]) + (d == 2 ? 0.0f : 0.5f)));
                        const float centerF = field.eval(center);
                        // Diagonal A = corners 0,2; diagonal B = corners 1,3.
                        bool sacrificeA;
                        if (std::fabs(centerF) > 1e-5f) {
                            const bool centerNeg = centerF < 0.0f;
                            sacrificeA = (neg[0] != centerNeg);
                        } else {
                            sacrificeA = std::fabs(vals[0]) + std::fabs(vals[2]) <
                                std::fabs(vals[1]) + std::fabs(vals[3]);
                        }
                        int c[4][3];
                        faceCorners(base, d, c);
                        const int k0 = sacrificeA ? 0 : 1;
                        const int k1 = sacrificeA ? 2 : 3;
                        // Try both candidates, keep the locally optimal flip.
                        int bestVictim = -1;
                        int bestCost = 100;
                        float bestAbs = 1e9f;
                        for (const int candidate : {k0, k1}) {
                            float& corner = valueAt(c[candidate][0], c[candidate][1], c[candidate][2]);
                            const float saved = corner;
                            corner = neg[candidate] ? std::fabs(saved) : -std::fabs(saved);
                            const int cost = touchingSaddles(c[candidate]);
                            corner = saved;
                            const float absVal = std::fabs(saved);
                            if (cost < bestCost || (cost == bestCost && absVal < bestAbs)) {
                                bestVictim = candidate;
                                bestCost = cost;
                                bestAbs = absVal;
                            }
                        }
                        float& corner = valueAt(c[bestVictim][0], c[bestVictim][1], c[bestVictim][2]);
                        // Flip the sign, keep the magnitude (surface moves < 1 cell).
                        corner = neg[bestVictim] ? std::fabs(corner) : -std::fabs(corner);
                        ++passFlips;
                    }
                }
            }
        }
        if (pass == 0) {
            out.saddleFaces = passSaddles;
        }
        out.flips += passFlips;
        out.passes = pass + 1;
        out.remaining = passSaddles;
        if (passSaddles == 0 || passFlips == 0) {
            break;
        }
    }
}

Mesh extractSurfaceNets(const CliffField& field, const std::vector<float>& samples,
    ExtractStats* stats) {
    const int nx = field.sizeX();
    const int ny = field.sizeY();
    const int nz = field.sizeZ();
    const int px = nx + 1;
    const int pz = nz + 1;
    const float cell = field.params().cellSize;
    const glm::vec3 org = field.origin();

    auto valueAt = [&](int x, int y, int z) -> float {
        return samples[(static_cast<size_t>(y) * pz + z) * px + x];
    };

    Mesh mesh;
    std::vector<int> voxelVertex(static_cast<size_t>(nx) * ny * nz, -1);
    auto vertAt = [&](int x, int y, int z) -> int& {
        return voxelVertex[(static_cast<size_t>(y) * nz + z) * nx + x];
    };

    // Pass 1: one vertex per sign-changing voxel at the mean of the edge
    // zero crossings; normals from central differences of the full field.
    const auto tv0 = Clock::now();
    int signVoxels = 0;
    const float eps = 0.5f * cell;
    for (int z = 0; z < nz; ++z) {
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                float cv[8];
                unsigned mask = 0;
                for (int c = 0; c < 8; ++c) {
                    cv[c] = valueAt(x + kCorner[c][0], y + kCorner[c][1], z + kCorner[c][2]);
                    if (cv[c] < 0.0f) {
                        mask |= 1u << c;
                    }
                }
                if (mask == 0u || mask == 0xFFu) {
                    continue;
                }
                ++signVoxels;
                glm::vec3 pos(0.0f);
                int crossings = 0;
                for (const auto& edge : kEdge) {
                    const float v0 = cv[edge[0]];
                    const float v1 = cv[edge[1]];
                    if ((v0 < 0.0f) == (v1 < 0.0f)) {
                        continue;
                    }
                    const float t = v0 / (v0 - v1);
                    const int* c0 = kCorner[edge[0]];
                    const int* c1 = kCorner[edge[1]];
                    const glm::vec3 p0(org.x + cell * static_cast<float>(x + c0[0]),
                        org.y + cell * static_cast<float>(y + c0[1]),
                        org.z + cell * static_cast<float>(z + c0[2]));
                    const glm::vec3 p1(org.x + cell * static_cast<float>(x + c1[0]),
                        org.y + cell * static_cast<float>(y + c1[1]),
                        org.z + cell * static_cast<float>(z + c1[2]));
                    pos = pos + (p0 + (p1 - p0) * t);
                    ++crossings;
                }
                pos = pos * (1.0f / static_cast<float>(crossings));
                // Smooth normals: central differences of the full field at the vertex.
                const glm::vec3 ex(eps, 0.0f, 0.0f);
                const glm::vec3 ey(0.0f, eps, 0.0f);
                const glm::vec3 ez(0.0f, 0.0f, eps);
                glm::vec3 n(field.eval(pos + ex) - field.eval(pos - ex),
                    field.eval(pos + ey) - field.eval(pos - ey),
                    field.eval(pos + ez) - field.eval(pos - ez));
                n = safeNormalize(n);
                MeshVertex vertex{pos.x, pos.y, pos.z, n.x, n.y, n.z, field.grooveDepth(pos)};
                vertAt(x, y, z) = static_cast<int>(mesh.vertices.size());
                mesh.vertices.push_back(vertex);
            }
        }
    }
    const auto tv1 = Clock::now();

    // Pass 2: one quad per sign-changing grid edge, wound counter-clockwise
    // seen from the positive (outside) side. For axis d the quad joins the
    // anchor voxel with its neighbours along u = (d+1)%3 and v = (d+2)%3.
    mesh.indices.reserve(mesh.vertices.size() * 6);
    for (int z = 0; z < nz; ++z) {
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                for (int d = 0; d < 3; ++d) {
                    const int u = (d + 1) % 3;
                    const int v = (d + 2) % 3;
                    const int c[3] = {x, y, z};
                    if (c[u] < 1 || c[v] < 1) {
                        continue; // boundary edges never cross the surface (padding)
                    }
                    int e0[3] = {x, y, z};
                    int e1[3] = {x, y, z};
                    e1[d] += 1;
                    const float f0 = valueAt(e0[0], e0[1], e0[2]);
                    const float f1 = valueAt(e1[0], e1[1], e1[2]);
                    if ((f0 < 0.0f) == (f1 < 0.0f)) {
                        continue;
                    }
                    int cu[3] = {x, y, z};
                    cu[u] -= 1;
                    int cv[3] = {x, y, z};
                    cv[v] -= 1;
                    int cuv[3] = {x, y, z};
                    cuv[u] -= 1;
                    cuv[v] -= 1;
                    const int i0 = vertAt(x, y, z);
                    const int i1 = vertAt(cu[0], cu[1], cu[2]);
                    const int i2 = vertAt(cuv[0], cuv[1], cuv[2]);
                    const int i3 = vertAt(cv[0], cv[1], cv[2]);
                    if (i0 < 0 || i1 < 0 || i2 < 0 || i3 < 0) {
                        continue; // should not happen: all four voxels share the edge
                    }
                    if (f0 < 0.0f) {
                        mesh.indices.push_back(static_cast<std::uint32_t>(i0));
                        mesh.indices.push_back(static_cast<std::uint32_t>(i1));
                        mesh.indices.push_back(static_cast<std::uint32_t>(i2));
                        mesh.indices.push_back(static_cast<std::uint32_t>(i0));
                        mesh.indices.push_back(static_cast<std::uint32_t>(i2));
                        mesh.indices.push_back(static_cast<std::uint32_t>(i3));
                    } else {
                        mesh.indices.push_back(static_cast<std::uint32_t>(i0));
                        mesh.indices.push_back(static_cast<std::uint32_t>(i3));
                        mesh.indices.push_back(static_cast<std::uint32_t>(i2));
                        mesh.indices.push_back(static_cast<std::uint32_t>(i0));
                        mesh.indices.push_back(static_cast<std::uint32_t>(i2));
                        mesh.indices.push_back(static_cast<std::uint32_t>(i1));
                    }
                }
            }
        }
    }
    const auto tv2 = Clock::now();

    if (stats != nullptr) {
        stats->signVoxels = signVoxels;
        stats->vertexMs = msSince(tv0, tv1);
        stats->quadMs = msSince(tv1, tv2);
    }
    return mesh;
}

WatertightReport checkWatertight(const Mesh& mesh) {
    struct DirCounts {
        int forward = 0;  // half-edge from the smaller to the larger index
        int backward = 0;
    };
    std::unordered_map<std::uint64_t, DirCounts> edgeMap;
    WatertightReport report;
    const std::size_t triCount = mesh.indices.size() / 3;
    for (std::size_t t = 0; t < triCount; ++t) {
        const std::uint32_t a = mesh.indices[t * 3 + 0];
        const std::uint32_t b = mesh.indices[t * 3 + 1];
        const std::uint32_t c = mesh.indices[t * 3 + 2];
        if (a == b || b == c || a == c) {
            ++report.degenerateTriangles;
            continue;
        }
        const std::uint32_t tri[3] = {a, b, c};
        for (int e = 0; e < 3; ++e) {
            const std::uint32_t from = tri[e];
            const std::uint32_t to = tri[(e + 1) % 3];
            const std::uint32_t lo = std::min(from, to);
            const std::uint32_t hi = std::max(from, to);
            const std::uint64_t key = (static_cast<std::uint64_t>(lo) << 32) | hi;
            DirCounts& counts = edgeMap[key];
            if (from == lo) {
                ++counts.forward;
            } else {
                ++counts.backward;
            }
            ++report.halfEdges;
        }
    }
    report.undirectedEdges = static_cast<int>(edgeMap.size());
    for (const auto& [key, counts] : edgeMap) {
        if (counts.forward != 1 || counts.backward != 1) {
            ++report.badEdges;
        }
        const int total = counts.forward + counts.backward;
        if (total == 1) {
            ++report.edgesWith1Half;
        } else if (total == 3) {
            ++report.edgesWith3Half;
        } else if (total >= 4) {
            ++report.edgesWith4Plus;
        }
    }
    return report;
}

} // namespace cliff
