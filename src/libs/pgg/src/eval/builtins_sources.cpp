#include "../../pch.h"

#include "builtins.h"

#include <cmath>
#include <unordered_map>

namespace pgg {
namespace {

// Position welder: shared corners/edges of generated grids collapse to one
// vertex. Coordinates are quantized to 1e-6 so 1-ulp differences between
// per-face computations of the same shared edge still weld.
struct VertexWelder {
    struct Key {
        int64_t x, y, z;
        bool operator==(const Key&) const = default;
    };
    struct KeyHash {
        size_t operator()(const Key& k) const {
            size_t h = std::hash<int64_t>()(k.x);
            h = h * 1000003 + std::hash<int64_t>()(k.y);
            h = h * 1000003 + std::hash<int64_t>()(k.z);
            return h;
        }
    };
    std::unordered_map<Key, int32_t, KeyHash> index;
    std::vector<glm::vec3> positions;

    int32_t add(const glm::vec3& p) {
        const Key k{static_cast<int64_t>(std::llround(p.x * 1e6)),
                    static_cast<int64_t>(std::llround(p.y * 1e6)),
                    static_cast<int64_t>(std::llround(p.z * 1e6))};
        auto [it, inserted] = index.emplace(k, static_cast<int32_t>(positions.size()));
        if (inserted) positions.push_back(p);
        return it->second;
    }
};

void addNormals(Geo& geo, std::vector<glm::vec3> normals) {
    geo.normals = std::make_shared<const std::vector<glm::vec3>>(std::move(normals));
}

}  // namespace

GeoPtr genIcoSphere(int subdiv, float radius) {
    subdiv = std::max(subdiv, 0);
    const float t = (1.0f + std::sqrt(5.0f)) * 0.5f;
    std::vector<glm::vec3> pos;
    for (const glm::vec3 v : {glm::vec3(-1, t, 0), glm::vec3(1, t, 0), glm::vec3(-1, -t, 0), glm::vec3(1, -t, 0),
                              glm::vec3(0, -1, t), glm::vec3(0, 1, t), glm::vec3(0, -1, -t), glm::vec3(0, 1, -t),
                              glm::vec3(t, 0, -1), glm::vec3(t, 0, 1), glm::vec3(-t, 0, -1), glm::vec3(-t, 0, 1)}) {
        pos.push_back(glm::normalize(v));
    }
    std::vector<int32_t> tris{
        0, 11, 5, 0, 5, 1, 0, 1, 7, 0, 7, 10, 0, 10, 11,
        1, 5, 9, 5, 11, 4, 11, 10, 2, 10, 7, 6, 7, 1, 8,
        3, 9, 4, 3, 4, 2, 3, 2, 6, 3, 6, 8, 3, 8, 9,
        4, 9, 5, 2, 4, 11, 6, 2, 10, 8, 6, 7, 9, 8, 1,
    };

    for (int s = 0; s < subdiv; ++s) {
        // Split every triangle into four; edge midpoints are welded via an
        // edge-keyed map so the mesh stays watertight by construction.
        std::unordered_map<int64_t, int32_t> midpoints;
        auto midpoint = [&](int32_t a, int32_t b) {
            const int64_t key = a < b ? (static_cast<int64_t>(a) << 32) | b
                                      : (static_cast<int64_t>(b) << 32) | a;
            auto [it, inserted] = midpoints.emplace(key, static_cast<int32_t>(pos.size()));
            if (inserted) pos.push_back(glm::normalize((pos[a] + pos[b]) * 0.5f));
            return it->second;
        };
        std::vector<int32_t> next;
        next.reserve(tris.size() * 4);
        for (size_t i = 0; i < tris.size(); i += 3) {
            const int32_t a = tris[i], b = tris[i + 1], c = tris[i + 2];
            const int32_t ab = midpoint(a, b);
            const int32_t bc = midpoint(b, c);
            const int32_t ca = midpoint(c, a);
            for (int32_t v : {a, ab, ca, b, bc, ab, c, ca, bc, ab, bc, ca}) next.push_back(v);
        }
        tris = std::move(next);
    }

    std::vector<glm::vec3> normals = pos;  // unit sphere: position == normal
    for (glm::vec3& p : pos) p *= radius;
    std::vector<int32_t> offsets;
    offsets.reserve(tris.size() / 3 + 1);
    for (size_t i = 0; i <= tris.size(); i += 3) offsets.push_back(static_cast<int32_t>(i));
    Geo geo;
    geo.kind = GeoKind::Mesh;
    geo.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pos));
    geo.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(tris));
    geo.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(offsets));
    addNormals(geo, std::move(normals));
    return std::make_shared<const Geo>(std::move(geo));
}

GeoPtr genBox(glm::vec3 size, int res) {
    res = std::max(res, 1);
    const glm::vec3 h = size * 0.5f;
    // Six faces as (origin, u, v) frames; corner order is CCW seen from
    // outside so Newell normals point outward.
    const glm::vec3 frames[6][4] = {
        // a                       b (a+u)                c                    d (a+v)
        {h * glm::vec3(1, -1, -1), h * glm::vec3(1, 1, -1), h * glm::vec3(1, 1, 1), h * glm::vec3(1, -1, 1)},    // +X
        {h * glm::vec3(-1, -1, 1), h * glm::vec3(-1, 1, 1), h * glm::vec3(-1, 1, -1), h * glm::vec3(-1, -1, -1)}, // -X
        {h * glm::vec3(-1, 1, -1), h * glm::vec3(-1, 1, 1), h * glm::vec3(1, 1, 1), h * glm::vec3(1, 1, -1)},    // +Y
        {h * glm::vec3(-1, -1, 1), h * glm::vec3(-1, -1, -1), h * glm::vec3(1, -1, -1), h * glm::vec3(1, -1, 1)}, // -Y
        {h * glm::vec3(-1, -1, 1), h * glm::vec3(1, -1, 1), h * glm::vec3(1, 1, 1), h * glm::vec3(-1, 1, 1)},    // +Z
        {h * glm::vec3(1, -1, -1), h * glm::vec3(-1, -1, -1), h * glm::vec3(-1, 1, -1), h * glm::vec3(1, 1, -1)}, // -Z
    };
    VertexWelder welder;
    std::vector<int32_t> corners;
    std::vector<int32_t> offsets{0};
    // Smooth vertex normals: each welded vertex accumulates the unit normal of
    // every side it belongs to (interior side vertices get the exact side
    // normal, edges the 45-degree bisector, corners the diagonal). Reading @N on
    // the faces domain averages the corner normals back to the exact side
    // normal, and displacing along @N keeps the box closed. A welded box has
    // no faceted point normals by construction; a faceted look is
    // compute_normals(mode = flat) or flat shading in the viewer.
    std::vector<glm::vec3> normals;
    const float invR = 1.0f / static_cast<float>(res);
    for (const auto& f : frames) {
        const glm::vec3 u = f[1] - f[0];
        const glm::vec3 v = f[3] - f[0];
        const glm::vec3 sideN = glm::normalize(glm::cross(u, v));
        std::vector<int32_t> grid(static_cast<size_t>(res + 1) * (res + 1));
        for (int j = 0; j <= res; ++j)
            for (int i = 0; i <= res; ++i) {
                const int32_t idx = welder.add(f[0] + u * (i * invR) + v * (j * invR));
                grid[static_cast<size_t>(j) * (res + 1) + i] = idx;
                if (static_cast<size_t>(idx) >= normals.size()) normals.resize(idx + 1, glm::vec3(0.0f));
                normals[static_cast<size_t>(idx)] += sideN;
            }
        for (int j = 0; j < res; ++j) {
            for (int i = 0; i < res; ++i) {
                const int32_t p00 = grid[static_cast<size_t>(j) * (res + 1) + i];
                const int32_t p10 = grid[static_cast<size_t>(j) * (res + 1) + i + 1];
                const int32_t p11 = grid[static_cast<size_t>(j + 1) * (res + 1) + i + 1];
                const int32_t p01 = grid[static_cast<size_t>(j + 1) * (res + 1) + i];
                for (int32_t c : {p00, p10, p11, p01}) corners.push_back(c);
                offsets.push_back(static_cast<int32_t>(corners.size()));
            }
        }
    }
    normals.resize(welder.positions.size(), glm::vec3(0.0f));
    for (glm::vec3& n : normals) n = glm::normalize(n);
    Geo geo;
    geo.kind = GeoKind::Mesh;
    geo.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(welder.positions));
    geo.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(corners));
    geo.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(offsets));
    addNormals(geo, std::move(normals));
    return std::make_shared<const Geo>(std::move(geo));
}

GeoPtr genGrid(glm::vec2 size, glm::vec2 resIn) {
    // Anisotropic resolution (v1.23): res.x cells along X, res.y along Z; a
    // scalar `res` broadcasts to both. Writes @uv (vec2, points): u along X,
    // v along Z, both 0..1 — the parametric coordinates every strip / leaf /
    // ribbon shape is written against.
    const int rx = std::max(static_cast<int>(std::lround(resIn.x)), 1);
    const int rz = std::max(static_cast<int>(std::lround(resIn.y)), 1);
    // Flat grid on the XZ plane (terrain base), +Y normals.
    std::vector<glm::vec3> pos(static_cast<size_t>(rx + 1) * (rz + 1));
    std::vector<glm::vec2> uv(pos.size());
    const float invX = 1.0f / static_cast<float>(rx);
    const float invZ = 1.0f / static_cast<float>(rz);
    for (int j = 0; j <= rz; ++j)
        for (int i = 0; i <= rx; ++i) {
            const size_t k = static_cast<size_t>(j) * (rx + 1) + i;
            const float u = i * invX, v = j * invZ;
            pos[k] = glm::vec3(-size.x * 0.5f + size.x * u, 0.0f, -size.y * 0.5f + size.y * v);
            uv[k] = glm::vec2(u, v);
        }
    std::vector<int32_t> corners;
    std::vector<int32_t> offsets{0};
    for (int j = 0; j < rz; ++j) {
        for (int i = 0; i < rx; ++i) {
            const int32_t p00 = j * (rx + 1) + i;
            const int32_t p10 = p00 + 1;
            const int32_t p11 = p00 + (rx + 1) + 1;
            const int32_t p01 = p00 + (rx + 1);
            // CCW seen from +Y.
            for (int32_t c : {p00, p01, p11, p10}) corners.push_back(c);
            offsets.push_back(static_cast<int32_t>(corners.size()));
        }
    }
    std::vector<glm::vec3> normals(pos.size(), glm::vec3(0, 1, 0));
    Geo geo;
    geo.kind = GeoKind::Mesh;
    geo.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pos));
    geo.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(corners));
    geo.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(offsets));
    addNormals(geo, std::move(normals));
    AttrSet attrs;
    attrs.columns["uv"] = AttrColumn{std::make_shared<const std::vector<glm::vec2>>(std::move(uv)), AttrTypeInfo::None};
    geo.pointAttrs = std::make_shared<const AttrSet>(std::move(attrs));
    return std::make_shared<const Geo>(std::move(geo));
}

GeoPtr genMeshLine(int count, float length, glm::vec3 dir) {
    // count <= 0 -> empty geo<points> (a legal empty seed for repeat
    // accumulators; a silent 1-point fallback used to leave a stray anchor at
    // the origin). count == 1 -> the single point at the origin.
    count = std::max(count, 0);
    glm::vec3 d(0, 0, 1);
    if (glm::dot(dir, dir) > 1e-12f) d = glm::normalize(dir);
    std::vector<glm::vec3> pos(static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) {
        const float t = count > 1 ? static_cast<float>(i) / static_cast<float>(count - 1) : 0.0f;
        pos[static_cast<size_t>(i)] = d * (length * t);
    }
    return makePoints(std::move(pos));
}

GeoPtr genPointCloud(int count, glm::vec3 bounds, Rng rng) {
    count = std::max(count, 0);
    std::vector<glm::vec3> pos(static_cast<size_t>(count));
    // Candidate address: (point_index, xyz lane) — spec §8.1.
    for (int i = 0; i < count; ++i) {
        for (uint32_t lane = 0; lane < 3; ++lane) {
            const float u = rngF32(rng, static_cast<uint64_t>(i), lane);
            pos[static_cast<size_t>(i)][static_cast<int>(lane)] =
                (u * 2.0f - 1.0f) * 0.5f * bounds[static_cast<int>(lane)];
        }
    }
    return makePoints(std::move(pos));
}

}  // namespace pgg
