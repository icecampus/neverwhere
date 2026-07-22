#include "pch.h"

#include "DebugDump.h"

#include <algorithm>
#include <unordered_map>

#include <spdlog/spdlog.h>

namespace cliff {

void debugDumpBadEdges(const CliffField& field, const std::vector<float>& samples,
    const Mesh& mesh, int maxCount) {
    struct DirCounts {
        int forward = 0;
        int backward = 0;
    };
    std::unordered_map<std::uint64_t, DirCounts> edgeMap;
    const std::size_t triCount = mesh.indices.size() / 3;
    for (std::size_t t = 0; t < triCount; ++t) {
        const std::uint32_t a = mesh.indices[t * 3 + 0];
        const std::uint32_t b = mesh.indices[t * 3 + 1];
        const std::uint32_t c = mesh.indices[t * 3 + 2];
        if (a == b || b == c || a == c) {
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
        }
    }

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
    auto voxelOf = [&](const MeshVertex& v, int out[3]) {
        out[0] = std::clamp(static_cast<int>((v.px - org.x) / cell), 0, nx - 1);
        out[1] = std::clamp(static_cast<int>((v.py - org.y) / cell), 0, ny - 1);
        out[2] = std::clamp(static_cast<int>((v.pz - org.z) / cell), 0, nz - 1);
    };

    int dumped = 0;
    int dumped4 = 0;
    for (const auto& [key, counts] : edgeMap) {
        if (counts.forward == 1 && counts.backward == 1) {
            continue;
        }
        const int total = counts.forward + counts.backward;
        const bool isCrack = (total < 4);
        if (isCrack && dumped >= maxCount) {
            continue;
        }
        if (!isCrack && dumped4 >= maxCount) {
            continue;
        }
        const std::uint32_t lo = static_cast<std::uint32_t>(key >> 32);
        const std::uint32_t hi = static_cast<std::uint32_t>(key & 0xFFFFFFFFu);
        const MeshVertex& a = mesh.vertices[lo];
        const MeshVertex& b = mesh.vertices[hi];
        spdlog::warn("bad edge fwd={} bwd={} | A({:.3f},{:.3f},{:.3f}) g={:.3f} n=({:.2f},{:.2f},{:.2f})"
            " | B({:.3f},{:.3f},{:.3f}) g={:.3f} n=({:.2f},{:.2f},{:.2f})",
            counts.forward, counts.backward,
            a.px, a.py, a.pz, a.groove, a.nx, a.ny, a.nz,
            b.px, b.py, b.pz, b.groove, b.nx, b.ny, b.nz);
        // Recover the shared voxel face and its corner signs.
        int va[3];
        int vb[3];
        voxelOf(a, va);
        voxelOf(b, vb);
        int axis = -1;
        for (int d = 0; d < 3; ++d) {
            if (va[d] != vb[d]) {
                axis = d;
            }
        }
        if (axis >= 0) {
            // Face corners: the 2x2 grid-point square between the two voxels.
            int base[3] = {std::min(va[0], vb[0]), std::min(va[1], vb[1]),
                std::min(va[2], vb[2])};
            base[axis] += 1; // the shared face sits at the higher grid plane
            const int u = (axis + 1) % 3;
            const int v = (axis + 2) % 3;
            char signs[5] = "????";
            float vals[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            glm::vec3 center(0.0f);
            int ci = 0;
            for (int du = 0; du <= 1; ++du) {
                for (int dv = 0; dv <= 1; ++dv) {
                    int c[3] = {base[0], base[1], base[2]};
                    c[u] += du;
                    c[v] += dv;
                    const float f = valueAt(c[0], c[1], c[2]);
                    vals[ci] = f;
                    signs[ci] = f < 0.0f ? '-' : '+';
                    center = center + glm::vec3(org.x + cell * c[0], org.y + cell * c[1],
                        org.z + cell * c[2]);
                    ++ci;
                }
            }
            center = center * 0.25f;
            spdlog::warn("    face axis={} voxA=({},{},{}) voxB=({},{},{}) signs={} "
                "vals=({:.4f},{:.4f},{:.4f},{:.4f}) centerF={:.4f}",
                axis, va[0], va[1], va[2], vb[0], vb[1], vb[2], signs,
                vals[0], vals[1], vals[2], vals[3], field.eval(center));
        }
        if (isCrack) {
            ++dumped;
        } else {
            ++dumped4;
        }
        if (dumped >= maxCount && dumped4 >= maxCount) {
            break;
        }
    }
}

} // namespace cliff
