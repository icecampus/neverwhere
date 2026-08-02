#include "render_core/scene_stitch.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace render_core {

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

float diamondNodeFill(const std::array<bool, 4>& nodeMask, glm::vec2 diamond) {
    const float wL = std::max(0.0f, -diamond.x);
    const float wR = std::max(0.0f, diamond.x);
    const float wU = std::max(0.0f, -diamond.y);
    const float wD = std::max(0.0f, diamond.y);
    const float sum = wL + wR + wU + wD;
    const float l = nodeMask[0] ? 1.0f : 0.0f;
    const float u = nodeMask[1] ? 1.0f : 0.0f;
    const float r = nodeMask[2] ? 1.0f : 0.0f;
    const float d = nodeMask[3] ? 1.0f : 0.0f;
    if (sum <= 1e-6f) {
        // The centre is where every wedge meets and the weights vanish; the
        // mean is the only value that does not favour one of them.
        return 0.25f * (l + u + r + d);
    }
    return (wL * l + wR * r + wU * u + wD * d) / sum;
}

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

void buildContactAoField(
    const AoFootprint* footprints,
    int count,
    int texelsPerCell,
    int marginCells,
    ContactAoField& out) {

    out = ContactAoField{};
    if (!footprints || count <= 0 || texelsPerCell <= 0) {
        return;
    }

    int mapW = 0;
    int mapH = 0;
    for (int i = 0; i < count; ++i) {
        if (footprints[i].nodeOn) {
            mapW = std::max(mapW, footprints[i].nodesX - 1);
            mapH = std::max(mapH, footprints[i].nodesY - 1);
        }
    }
    if (mapW <= 0 || mapH <= 0) {
        return;
    }

    const int margin = std::max(marginCells, 0);
    const int cellsX = mapW + 2 * margin;
    const int cellsZ = mapH + 2 * margin;
    const int w = cellsX * texelsPerCell;
    const int h = cellsZ * texelsPerCell;

    // Corner states of a cell in the cellCornerNodes slot order [Left, Up,
    // Right, Down]; out-of-bounds nodes count as off, like the playground's
    // LandBrush::nodeIsOn did.
    const auto nodeMaskAt = [](const AoFootprint& fp, int cx, int cz) {
        const auto on = [&fp](int x, int y) {
            return x >= 0 && y >= 0 && x < fp.nodesX && y < fp.nodesY && fp.nodeOn(x, y);
        };
        return std::array<bool, 4>{
            on(cx, cz + 1),     // Left
            on(cx, cz),         // Up
            on(cx + 1, cz),     // Right
            on(cx + 1, cz + 1), // Down
        };
    };

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
            for (int i = 0; i < count && maskCount < 8; ++i) {
                if (!footprints[i].nodeOn) {
                    continue;
                }
                const std::array<bool, 4> m = nodeMaskAt(footprints[i], cx, cz);
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
        return;
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

    out.width = w;
    out.height = h;
    out.cellsPerTexel = 1.0f / static_cast<float>(texelsPerCell);
    out.originX = static_cast<float>(-margin);
    out.originZ = static_cast<float>(-margin);
    out.texels.resize(dist.size());
    for (std::size_t i = 0; i < dist.size(); ++i) {
        const float cells = dist[i] * out.cellsPerTexel;
        const float norm = std::clamp(cells / kAoMaxDistanceCells, 0.0f, 1.0f);
        out.texels[i] = static_cast<std::uint8_t>(std::lround(norm * 255.0f));
    }
}

float isoHeightToWorld(float halfW, float halfH, float heightScale) {
    const float denom = 2.0f * (halfW * halfW - halfH * halfH);
    if (denom <= 1e-4f) {
        return 1.0f;
    }
    return heightScale / std::sqrt(denom);
}

glm::vec3 SceneStitchSettings::sunDirection() const {
    const float ce = std::cos(lightElevation);
    return glm::normalize(glm::vec3{
        ce * std::sin(lightAzimuth), std::sin(lightElevation), ce * std::cos(lightAzimuth)});
}

SceneStitchParams buildStitchParams(const SceneStitchSettings& settings, const ContactAoField& aoField) {
    SceneStitchParams p{};
    const glm::vec3 sun = settings.sunDirection();
    p.sunDir[0] = sun.x;
    p.sunDir[1] = sun.y;
    p.sunDir[2] = sun.z;

    if (aoField.empty()) {
        // The renderer binds a 1x1 "far" placeholder: any uv clamps to
        // "nothing nearby".
        p.aoRect[2] = 1.0f;
        p.aoRect[3] = 1.0f;
    } else {
        p.aoRect[0] = aoField.originX;
        p.aoRect[1] = aoField.originZ;
        p.aoRect[2] = 1.0f / aoField.extentX();
        p.aoRect[3] = 1.0f / aoField.extentZ();
    }

    // The ground half of the block; the cliff pass keeps its own palette
    // ambient/diffuse/gamma.
    p.params0[0] = settings.groundLit ? settings.ambient : 1.0f;
    p.params0[1] = settings.groundLit ? settings.diffuse : 0.0f;
    p.params0[2] = settings.groundLit ? settings.gamma : 1.0f;
    p.params1[2] = settings.aoEnabled ? settings.aoStrength : 0.0f;
    p.params1[3] = settings.aoRadius;
    return p;
}

} // namespace render_core
