#include "pch.h"

#include "stone_gen/stone_bake.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>

#include <glm/glm.hpp>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace stone_gen {

namespace {

std::uint8_t toByte(float v) {
    return static_cast<std::uint8_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
}

struct Texel {
    glm::vec3 pos;
    glm::vec3 normal;
    bool covered = false;
};

} // namespace

BakedTextures bakeTextures(const StoneSdf& sdf, const StoneMesh& mesh,
    const BakeParams& params) {
    const auto t0 = std::chrono::steady_clock::now();
    const int size = params.textureSize;

    // Pass 1: rasterize world-space attributes into UV texels.
    std::vector<Texel> grid(static_cast<size_t>(size) * size);
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const StoneMeshVertex& v0 = mesh.vertices[mesh.indices[t + 0]];
        const StoneMeshVertex& v1 = mesh.vertices[mesh.indices[t + 1]];
        const StoneMeshVertex& v2 = mesh.vertices[mesh.indices[t + 2]];
        const glm::vec2 uv0 = v0.uv * static_cast<float>(size - 1);
        const glm::vec2 uv1 = v1.uv * static_cast<float>(size - 1);
        const glm::vec2 uv2 = v2.uv * static_cast<float>(size - 1);

        const float minXf = std::min({uv0.x, uv1.x, uv2.x});
        const float minYf = std::min({uv0.y, uv1.y, uv2.y});
        const float maxXf = std::max({uv0.x, uv1.x, uv2.x});
        const float maxYf = std::max({uv0.y, uv1.y, uv2.y});
        const int minX = std::max(0, static_cast<int>(std::floor(minXf)));
        const int minY = std::max(0, static_cast<int>(std::floor(minYf)));
        const int maxX = std::min(size - 1, static_cast<int>(std::ceil(maxXf)));
        const int maxY = std::min(size - 1, static_cast<int>(std::ceil(maxYf)));

        const glm::vec2 e1 = uv1 - uv0;
        const glm::vec2 e2 = uv2 - uv0;
        const float denom = e1.x * e2.y - e1.y * e2.x;
        if (std::fabs(denom) < 1e-9f) {
            continue;
        }
        for (int y = minY; y <= maxY; ++y) {
            for (int x = minX; x <= maxX; ++x) {
                const glm::vec2 d = glm::vec2(static_cast<float>(x) + 0.5f,
                    static_cast<float>(y) + 0.5f) - uv0;
                const float w1 = (d.x * e2.y - d.y * e2.x) / denom;
                const float w2 = (e1.x * d.y - e1.y * d.x) / denom;
                const float w0 = 1.0f - w1 - w2;
                const float eps = -1e-4f;
                if (w0 < eps || w1 < eps || w2 < eps) {
                    continue;
                }
                Texel& tex = grid[static_cast<size_t>(y) * size + x];
                tex.pos = v0.pos * w0 + v1.pos * w1 + v2.pos * w2;
                tex.normal = glm::normalize(v0.normal * w0 + v1.normal * w1 +
                    v2.normal * w2);
                tex.covered = true;
            }
        }
    }

    // Pass 2: material evaluation per covered texel.
    BakedTextures out;
    out.size = size;
    out.albedo.assign(static_cast<size_t>(size) * size * 4, 0);
    out.normal.assign(static_cast<size_t>(size) * size * 4, 0);
    std::vector<std::uint8_t> covered(static_cast<size_t>(size) * size, 0);

    // World units per texel: atlas tiles are size/3 x size/2 texels and each
    // covers its face's extents (same boxHalf as the UV mapping in
    // generateMesh). The bump gradient stencil spans ~1 texel so above-
    // Nyquist fbm octaves average out (per-texel stencil = rainbow noise).
    const float uvBulge = sdf.params().shape1[2] + sdf.params().boxSize[3] +
        sdf.params().shape2[0];
    const glm::vec3 boxHalf(sdf.params().boxSize[0] + uvBulge,
        sdf.params().boxSize[1] + uvBulge, sdf.params().boxSize[2] + uvBulge);
    const float tileW = static_cast<float>(size) / 3.0f;
    const float tileH = static_cast<float>(size) / 2.0f;
    const float texelWorld = std::max(
        2.0f * std::max(boxHalf.x, boxHalf.z) / tileW,
        2.0f * std::max(boxHalf.y, boxHalf.z) / tileH);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const size_t index = static_cast<size_t>(y) * size + x;
            const Texel& tex = grid[index];
            if (!tex.covered) {
                continue;
            }
            covered[index] = 1;
            float dist = 0.0f;
            float cellF = 0.0f;
            float cellId = 0.0f;
            sdf.map(tex.pos, dist, cellF, cellId);
            const glm::vec3 alb = sdf.albedo(tex.pos, tex.normal, cellF, cellId);
            const float ao = sdf.ambientOcclusion(tex.pos, tex.normal, params.aoTaps);
            const glm::vec3 nb = sdf.bumpNormal(tex.pos, sdf.normal(tex.pos),
                2.0f * texelWorld);

            std::uint8_t* a = &out.albedo[index * 4];
            a[0] = toByte(alb.r);
            a[1] = toByte(alb.g);
            a[2] = toByte(alb.b);
            a[3] = toByte(ao);
            std::uint8_t* n = &out.normal[index * 4];
            n[0] = toByte(nb.x * 0.5f + 0.5f);
            n[1] = toByte(nb.y * 0.5f + 0.5f);
            n[2] = toByte(nb.z * 0.5f + 0.5f);
            n[3] = 255;
        }
    }

    // Pass 3: dilation — fill uncovered texels from covered neighbours so
    // mipmapped sampling never pulls black into the island edges.
    for (int ring = 0; ring < params.dilationPx; ++ring) {
        std::vector<std::uint8_t> next = covered;
        for (int y = 0; y < size; ++y) {
            for (int x = 0; x < size; ++x) {
                const size_t index = static_cast<size_t>(y) * size + x;
                if (covered[index] != 0) {
                    continue;
                }
                const int dx[4] = {1, -1, 0, 0};
                const int dy[4] = {0, 0, 1, -1};
                for (int k = 0; k < 4; ++k) {
                    const int nx = x + dx[k];
                    const int ny = y + dy[k];
                    if (nx < 0 || ny < 0 || nx >= size || ny >= size) {
                        continue;
                    }
                    const size_t ni = static_cast<size_t>(ny) * size + nx;
                    if (covered[ni] == 0) {
                        continue;
                    }
                    for (int c = 0; c < 4; ++c) {
                        out.albedo[index * 4 + c] = out.albedo[ni * 4 + c];
                        out.normal[index * 4 + c] = out.normal[ni * 4 + c];
                    }
                    next[index] = 1;
                    break;
                }
            }
        }
        covered = std::move(next);
    }

    const auto t1 = std::chrono::steady_clock::now();
    out.bakeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return out;
}

bool writePng(const std::string& path, int w, int h, const std::vector<std::uint8_t>& rgba) {
    if (rgba.size() != static_cast<size_t>(w) * h * 4) {
        return false;
    }
    return stbi_write_png(path.c_str(), w, h, 4, rgba.data(), w * 4) != 0;
}

bool writeObj(const std::string& path, const StoneMesh& mesh) {
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        return false;
    }
    out << "# stone_gen stone cube\n";
    for (const StoneMeshVertex& v : mesh.vertices) {
        out << "v " << v.pos.x << ' ' << v.pos.y << ' ' << v.pos.z << '\n';
    }
    for (const StoneMeshVertex& v : mesh.vertices) {
        // Standard OBJ convention: vt v0 = image bottom; the baker's row 0
        // is uv v0, so flip here and generic viewers match the PNGs.
        out << "vt " << v.uv.x << ' ' << 1.0f - v.uv.y << '\n';
    }
    for (const StoneMeshVertex& v : mesh.vertices) {
        out << "vn " << v.normal.x << ' ' << v.normal.y << ' ' << v.normal.z << '\n';
    }
    for (size_t t = 0; t + 2 < mesh.indices.size(); t += 3) {
        const std::uint32_t a = mesh.indices[t + 0] + 1;
        const std::uint32_t b = mesh.indices[t + 1] + 1;
        const std::uint32_t c = mesh.indices[t + 2] + 1;
        out << "f " << a << '/' << a << '/' << a << ' '
            << b << '/' << b << '/' << b << ' '
            << c << '/' << c << '/' << c << '\n';
    }
    return static_cast<bool>(out);
}

} // namespace stone_gen
