#include "SceneStitch.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "LandBrush.h"

namespace {

constexpr float kDiag = 1.41421356f;

void chamferPass(std::vector<float>& d, int w, int h) {
    const auto at = [&](int x, int z) -> float& { return d[static_cast<std::size_t>(z) * w + x]; };
    const auto probe = [&](int x, int z, float add, float& best) {
        if (x < 0 || z < 0 || x >= w || z >= h) {
            return;
        }
        best = std::min(best, at(x, z) + add);
    };

    for (int z = 0; z < h; ++z) {
        for (int x = 0; x < w; ++x) {
            float best = at(x, z);
            probe(x - 1, z, 1.0f, best);
            probe(x, z - 1, 1.0f, best);
            probe(x - 1, z - 1, kDiag, best);
            probe(x + 1, z - 1, kDiag, best);
            at(x, z) = best;
        }
    }
    for (int z = h - 1; z >= 0; --z) {
        for (int x = w - 1; x >= 0; --x) {
            float best = at(x, z);
            probe(x + 1, z, 1.0f, best);
            probe(x, z + 1, 1.0f, best);
            probe(x + 1, z + 1, kDiag, best);
            probe(x - 1, z + 1, kDiag, best);
            at(x, z) = best;
        }
    }
}

} // namespace

float ContactAoField::distanceAt(float worldX, float worldZ) const {
    if (empty() || cellsPerTexel <= 0.0f) {
        return kAoMaxDistanceCells;
    }
    const int tx = static_cast<int>(std::floor((worldX - originX) / cellsPerTexel));
    const int tz = static_cast<int>(std::floor((worldZ - originZ) / cellsPerTexel));
    if (tx < 0 || tz < 0 || tx >= width || tz >= height) {
        return kAoMaxDistanceCells;
    }
    const std::uint8_t raw = texels[static_cast<std::size_t>(tz) * width + tx];
    return (static_cast<float>(raw) / 255.0f) * kAoMaxDistanceCells;
}

ContactAoField buildContactAoField(
    const LandBrush* const* brushes,
    int brushCount,
    int texelsPerCell,
    int marginCells) {

    ContactAoField field;
    if (!brushes || brushCount <= 0 || texelsPerCell <= 0) {
        return field;
    }

    int mapW = 0;
    int mapH = 0;
    for (int i = 0; i < brushCount; ++i) {
        if (brushes[i]) {
            mapW = std::max(mapW, brushes[i]->width());
            mapH = std::max(mapH, brushes[i]->height());
        }
    }
    if (mapW <= 0 || mapH <= 0) {
        return field;
    }

    const int margin = std::max(marginCells, 0);
    const int cellsX = mapW + 2 * margin;
    const int cellsZ = mapH + 2 * margin;
    const int w = cellsX * texelsPerCell;
    const int h = cellsZ * texelsPerCell;

    // Cell (cx, cz) owns the world square [cx, cx+1] x [cz, cz+1] — the same
    // node-based frame the surface-nets mesh lives in. Coverage is sampled per
    // texel through the partial-fill rule rather than per cell: a half-lit
    // cell is a wedge, and treating it as a full square pushes the darkening
    // out to the cell border, so the ring squares off instead of tracing the
    // wall. Stored as coverage - 0.5, i.e. signed with the outline at zero.
    const float texelSize = 1.0f / static_cast<float>(texelsPerCell);
    std::vector<float> cov(static_cast<std::size_t>(w) * h, -0.5f);
    bool anyCovered = false;
    for (int cz = -margin; cz < mapH + margin; ++cz) {
        for (int cx = -margin; cx < mapW + margin; ++cx) {
            std::array<bool, 4> masks[8];
            int maskCount = 0;
            for (int i = 0; i < brushCount && maskCount < 8; ++i) {
                if (!brushes[i]) {
                    continue;
                }
                const std::array<bool, 4> m = brushes[i]->nodeMaskAt({cx, cz});
                if (m[0] || m[1] || m[2] || m[3]) {
                    masks[maskCount++] = m;
                }
            }
            if (maskCount == 0) {
                continue;
            }
            const int tx0 = (cx + margin) * texelsPerCell;
            const int tz0 = (cz + margin) * texelsPerCell;
            for (int ty = 0; ty < texelsPerCell; ++ty) {
                for (int tx = 0; tx < texelsPerCell; ++tx) {
                    const glm::vec2 uv{
                        (static_cast<float>(tx) + 0.5f) * texelSize,
                        (static_cast<float>(ty) + 0.5f) * texelSize};
                    const glm::vec2 diamond = cellSquareToDiamond(uv);
                    float fill = 0.0f;
                    for (int m = 0; m < maskCount; ++m) {
                        fill = std::max(fill, diamondNodeFill(masks[m], diamond));
                    }
                    anyCovered = anyCovered || fill >= 0.5f;
                    cov[static_cast<std::size_t>(tz0 + ty) * w + tx0 + tx] = fill - 0.5f;
                }
            }
        }
    }
    if (!anyCovered) {
        return field;
    }

    // Seed the transform. Texels straight outside the outline get the linear
    // crossing point towards their solid neighbour instead of a flat 1: the
    // outline runs at any angle through the grid, and rounding it to whole
    // texels is what leaves a staircase along diagonal walls.
    const float kFar = std::numeric_limits<float>::max() * 0.25f;
    std::vector<float> dist(cov.size(), kFar);
    for (int z = 0; z < h; ++z) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(z) * w + x;
            if (cov[i] >= 0.0f) {
                dist[i] = 0.0f;
                continue;
            }
            const int nx[4] = {x - 1, x + 1, x, x};
            const int nz[4] = {z, z, z - 1, z + 1};
            for (int n = 0; n < 4; ++n) {
                if (nx[n] < 0 || nz[n] < 0 || nx[n] >= w || nz[n] >= h) {
                    continue;
                }
                const float other = cov[static_cast<std::size_t>(nz[n]) * w + nx[n]];
                if (other < 0.0f) {
                    continue;
                }
                const float span = -cov[i] + other;
                dist[i] = std::min(dist[i], span > 1e-6f ? -cov[i] / span : 0.0f);
            }
        }
    }

    chamferPass(dist, w, h);

    field.width = w;
    field.height = h;
    field.cellsPerTexel = 1.0f / static_cast<float>(texelsPerCell);
    field.originX = static_cast<float>(-margin);
    field.originZ = static_cast<float>(-margin);
    field.texels.resize(dist.size());
    for (std::size_t i = 0; i < dist.size(); ++i) {
        const float cells = dist[i] * field.cellsPerTexel;
        const float norm = std::clamp(cells / kAoMaxDistanceCells, 0.0f, 1.0f);
        field.texels[i] = static_cast<std::uint8_t>(std::lround(norm * 255.0f));
    }
    return field;
}

glm::vec3 SunBasis::project(const glm::vec3& worldPos) const {
    const glm::vec4 p{worldPos, 1.0f};
    return {glm::dot(row0, p), glm::dot(row1, p), glm::dot(row2, p)};
}

SunBasis buildSunBasis(const glm::vec3& sunDir, const glm::vec3& boxMin, const glm::vec3& boxMax) {
    SunBasis basis;
    const float len = glm::length(sunDir);
    if (len < 1e-5f) {
        return basis;
    }
    basis.dir = sunDir / len;
    // Straight-overhead sun would make cross() degenerate with world up.
    const glm::vec3 helper = (std::abs(basis.dir.y) > 0.99f)
        ? glm::vec3{0.0f, 0.0f, 1.0f}
        : glm::vec3{0.0f, 1.0f, 0.0f};
    basis.right = glm::normalize(glm::cross(helper, basis.dir));
    basis.up = glm::cross(basis.dir, basis.right);

    float uMin = std::numeric_limits<float>::max();
    float uMax = std::numeric_limits<float>::lowest();
    float vMin = uMin;
    float vMax = uMax;
    float dMin = uMin;
    float dMax = uMax;
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 c{
            (corner & 1) ? boxMax.x : boxMin.x,
            (corner & 2) ? boxMax.y : boxMin.y,
            (corner & 4) ? boxMax.z : boxMin.z};
        const float u = glm::dot(c, basis.right);
        const float v = glm::dot(c, basis.up);
        const float d = -glm::dot(c, basis.dir);
        uMin = std::min(uMin, u);
        uMax = std::max(uMax, u);
        vMin = std::min(vMin, v);
        vMax = std::max(vMax, v);
        dMin = std::min(dMin, d);
        dMax = std::max(dMax, d);
    }

    const float uSpan = std::max(uMax - uMin, 1e-3f);
    const float vSpan = std::max(vMax - vMin, 1e-3f);
    const float dSpan = std::max(dMax - dMin, 1e-3f);
    basis.row0 = glm::vec4(basis.right / uSpan, -uMin / uSpan);
    basis.row1 = glm::vec4(basis.up / vSpan, -vMin / vSpan);
    basis.row2 = glm::vec4(-basis.dir / dSpan, -dMin / dSpan);
    basis.valid = true;
    return basis;
}

float isoHeightToWorld(float halfW, float halfH, float heightScale) {
    const float denom = 2.0f * (halfW * halfW - halfH * halfH);
    if (denom <= 1e-4f) {
        return 1.0f;
    }
    return heightScale / std::sqrt(denom);
}
