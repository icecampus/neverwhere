#include "pch.h"

#include "stone_gen/stone_mesh.h"

#include <chrono>
#include <cmath>
#include <unordered_map>

#include <highground_core/surface_nets.h>

namespace stone_gen {

namespace {

int dominantAxis(const glm::vec3& n) {
    const glm::vec3 a = glm::abs(n);
    if (a.x >= a.y && a.x >= a.z) {
        return 0;
    }
    return a.y >= a.z ? 1 : 2;
}

// Planar projection onto the given axis plane (same convention as
// boxProjectUv below), normalized to [0,1] over the reference box.
glm::vec2 projectOnAxis(const glm::vec3& p, float sign, int axis, const glm::vec3& half) {
    glm::vec2 uv;
    if (axis == 0) {
        uv = glm::vec2(sign > 0.0f ? -p.z : p.z, p.y);
        uv /= 2.0f * glm::vec2(half.z, half.y);
    } else if (axis == 1) {
        uv = glm::vec2(p.x, sign > 0.0f ? -p.z : p.z);
        uv /= 2.0f * glm::vec2(half.x, half.z);
    } else {
        uv = glm::vec2(sign > 0.0f ? p.x : -p.x, p.y);
        uv /= 2.0f * glm::vec2(half.x, half.y);
    }
    return uv + 0.5f;
}

// Box projection by the dominant normal axis, normalized per-face to [0,1]
// (uniform texel density on the reference box; rocks are 3D-baked, so the
// edge seams are nearly invisible).
glm::vec2 boxProjectUv(const glm::vec3& p, const glm::vec3& n, const glm::vec3& half) {
    const int axis = dominantAxis(n);
    return projectOnAxis(p, n[axis], axis, half);
}

// Triangles straddling the box-projection seams (corners with different
// dominant axes) stretch across the whole texture otherwise. Clone the
// offending corners with UVs re-projected onto the face's own axis.
void splitSeamVertices(StoneMesh& mesh, const glm::vec3& half) {
    std::unordered_map<std::uint64_t, std::uint32_t> clones;
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const glm::vec3& p0 = mesh.vertices[mesh.indices[t + 0]].pos;
        const glm::vec3& p1 = mesh.vertices[mesh.indices[t + 1]].pos;
        const glm::vec3& p2 = mesh.vertices[mesh.indices[t + 2]].pos;
        const glm::vec3 faceN = glm::cross(p1 - p0, p2 - p0);
        const int faceAxis = dominantAxis(faceN);
        for (int k = 0; k < 3; ++k) {
            std::uint32_t& index = mesh.indices[t + k];
            const StoneMeshVertex& v = mesh.vertices[index];
            if (dominantAxis(v.normal) == faceAxis) {
                continue;
            }
            const std::uint64_t key =
                (static_cast<std::uint64_t>(index) << 2) | static_cast<std::uint64_t>(faceAxis);
            auto it = clones.find(key);
            if (it == clones.end()) {
                StoneMeshVertex clone = v;
                // The sign must be the FACE's: corner vertices at edges have
                // normals between faces, their own sign flips per triangle
                // and mirrors the projection into a patchwork.
                clone.uv = projectOnAxis(v.pos, faceN[faceAxis], faceAxis, half);
                const std::uint32_t newIndex = static_cast<std::uint32_t>(mesh.vertices.size());
                mesh.vertices.push_back(clone);
                it = clones.emplace(key, newIndex).first;
            }
            index = it->second;
        }
    }
}

} // namespace

StoneMesh generateMesh(const StoneSdf& sdf, const MeshParams& meshParams) {
    const auto t0 = std::chrono::steady_clock::now();
    const StoneCubeParams& params = sdf.params();

    // Field domain: cube bbox + bulge/detail + padding.
    const float bulge = params.shape1[2] + params.boxSize[3] + params.shape2[0] +
        meshParams.padding;
    const glm::vec3 half(params.boxSize[0] + bulge, params.boxSize[1] + bulge,
        params.boxSize[2] + bulge);
    const float cell = meshParams.cellSize;

    cliff::ScalarFieldView field;
    field.origin = -half;
    field.cellSize = cell;
    field.nx = static_cast<int>(std::ceil(2.0f * half.x / cell));
    field.ny = static_cast<int>(std::ceil(2.0f * half.y / cell));
    field.nz = static_cast<int>(std::ceil(2.0f * half.z / cell));
    field.eval = [&sdf](const glm::vec3& p) { return sdf.eval(p); };
    field.grooveDepth = [&sdf](const glm::vec3& p) {
        float d = 0.0f;
        float f = 0.0f;
        float id = 0.0f;
        sdf.map(p, d, f, id);
        return f;
    };

    const int px = field.nx + 1;
    const int py = field.ny + 1;
    const int pz = field.nz + 1;
    std::vector<float> samples(static_cast<size_t>(px) * py * pz);
    for (int iy = 0; iy < py; ++iy) {
        const float y = field.origin.y + iy * cell;
        for (int iz = 0; iz < pz; ++iz) {
            const float z = field.origin.z + iz * cell;
            float* row = &samples[(static_cast<size_t>(iy) * pz + iz) * px];
            for (int ix = 0; ix < px; ++ix) {
                row[ix] = sdf.eval(glm::vec3(field.origin.x + ix * cell, y, z));
            }
        }
    }

    // Gentle blur over the sampled field: the stone SDF has crease-like
    // voronoi grooves that alias into terracing on naive surface nets.
    // (Same trick the cliff field gets for free from its blurred nodes.)
    auto sampleAt = [&samples, px, pz](int x, int y, int z) -> float& {
        return samples[(static_cast<size_t>(y) * pz + z) * px + x];
    };
    std::vector<float> tmp(samples.size());
    for (int pass = 0; pass < meshParams.blurPasses; ++pass) {
        for (int axis = 0; axis < 3; ++axis) {
            const int dims[3] = {px, py, pz};
            for (int y = 0; y < py; ++y) {
                for (int z = 0; z < pz; ++z) {
                    for (int x = 0; x < px; ++x) {
                        int c[3] = {x, y, z};
                        float sum = 0.0f;
                        for (int k = -1; k <= 1; ++k) {
                            int q[3] = {c[0], c[1], c[2]};
                            q[axis] = std::clamp(q[axis] + k, 0, dims[axis] - 1);
                            sum += sampleAt(q[0], q[1], q[2]);
                        }
                        tmp[(static_cast<size_t>(y) * pz + z) * px + x] = sum / 3.0f;
                    }
                }
            }
            samples = tmp;
        }
    }

    cliff::RegularizeStats regStats;
    cliff::regularizeSigns(field, samples, &regStats);
    const cliff::Mesh mesh = cliff::extractSurfaceNets(field, samples, nullptr);
    const cliff::WatertightReport report = cliff::checkWatertight(mesh);

    StoneMesh out;
    out.watertightBadEdges = report.badEdges;
    out.remainingSaddles = regStats.remaining;
    out.vertices.reserve(mesh.vertices.size());
    // UV extents cover the bulged surface, not just the reference box,
    // so protruding rocks stay inside [0,1] (no edge clamping artifacts).
    const float uvBulge = params.shape1[2] + params.boxSize[3] + params.shape2[0];
    const glm::vec3 boxHalf(params.boxSize[0] + uvBulge, params.boxSize[1] + uvBulge,
        params.boxSize[2] + uvBulge);
    for (const cliff::MeshVertex& v : mesh.vertices) {
        StoneMeshVertex sv;
        sv.pos = glm::vec3(v.px, v.py, v.pz);
        sv.normal = glm::vec3(v.nx, v.ny, v.nz);
        sv.cellFactor = v.groove; // cell factor was mapped onto the groove slot
        sv.uv = boxProjectUv(sv.pos, sv.normal, boxHalf);
        out.vertices.push_back(sv);
    }
    out.indices = mesh.indices;
    splitSeamVertices(out, boxHalf);

    const auto t1 = std::chrono::steady_clock::now();
    out.meshMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return out;
}

} // namespace stone_gen
