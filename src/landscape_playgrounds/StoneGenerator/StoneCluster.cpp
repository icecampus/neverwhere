#include "pch.h"

#include "StoneCluster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>

namespace {

// [0, 1) from the raw mt19937_64 output (STL-independent mapping).
double uni01(std::mt19937_64& rng) {
    return static_cast<double>(rng() >> 11) * (1.0 / 9007199254740992.0);
}

std::uint64_t mixSeed(int seed) {
    return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(seed)) + 0x9E3779B9ull) *
        0xBF58476D1CE4E5B9ull;
}

// Node (i,j) -> world XZ: nodeToField, then the inverse of the renderer's
// world -> field mapping (same math as the single-rock anchor).
glm::vec2 nodeWorldXZ(const topology_core::DiamondIsometry& iso, glm::ivec2 node) {
    const glm::vec2 f = iso.nodeToField(node);
    const float halfW = iso.dims.cellSize().x * 0.5f;
    const float halfH = iso.dims.cellSize().y * 0.5f;
    const float a = (f.x - halfW) / halfW; // x - z
    const float b = (f.y - halfH) / halfH; // x + z
    return {(a + b) * 0.5f, (b - a) * 0.5f};
}

// 8-connected chamfer distance (in node units) from each ON node to the
// nearest off node; two passes, weights (1, sqrt(2)). Off nodes stay 0.
std::vector<float> silhouetteDt(const NodeField& nodes) {
    const int w = nodes.width;
    const int h = nodes.height;
    const float inf = 1e9f;
    const float diag = 1.41421356237f;
    std::vector<float> d(static_cast<std::size_t>(w) * h);
    for (int i = 0; i < w * h; ++i) {
        d[static_cast<std::size_t>(i)] = nodes.nodes[static_cast<std::size_t>(i)] ? inf : 0.0f;
    }
    const auto at = [&](int x, int y) -> float& {
        return d[static_cast<std::size_t>(y) * w + x];
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float v = at(x, y);
            if (x > 0) v = std::min(v, at(x - 1, y) + 1.0f);
            if (y > 0) v = std::min(v, at(x, y - 1) + 1.0f);
            if (x > 0 && y > 0) v = std::min(v, at(x - 1, y - 1) + diag);
            if (x + 1 < w && y > 0) v = std::min(v, at(x + 1, y - 1) + diag);
            at(x, y) = v;
        }
    }
    for (int y = h - 1; y >= 0; --y) {
        for (int x = w - 1; x >= 0; --x) {
            float v = at(x, y);
            if (x + 1 < w) v = std::min(v, at(x + 1, y) + 1.0f);
            if (y + 1 < h) v = std::min(v, at(x, y + 1) + 1.0f);
            if (x + 1 < w && y + 1 < h) v = std::min(v, at(x + 1, y + 1) + diag);
            if (x > 0 && y + 1 < h) v = std::min(v, at(x - 1, y + 1) + diag);
            at(x, y) = v;
        }
    }
    return d;
}

} // namespace

StoneClusterResult buildCluster(
    const NodeField& nodes,
    const topology_core::DiamondIsometry& iso,
    const StoneGenParams& gen,
    const StoneClusterParams& cl) {

    StoneClusterResult out;
    std::mt19937_64 rng(mixSeed(gen.seed) ^ 0xC1A57E11u);

    // Candidate sites: every painted node, sized by the silhouette DT.
    const std::vector<float> dt = silhouetteDt(nodes);
    struct Site {
        glm::vec2 xz;
        float radius;
        float jitter;
    };
    std::vector<Site> candidates;
    for (int y = 0; y < nodes.height; ++y) {
        for (int x = 0; x < nodes.width; ++x) {
            if (!nodes.isOn({x, y})) {
                continue;
            }
            const float d = dt[static_cast<std::size_t>(y) * nodes.width + x];
            // Superlinear size curve: deep silhouette cores get disproportionately
            // big monoliths, the border stays pebbles (the reference contrast).
            const float r = std::clamp(0.30f * std::pow(d, 1.35f) * cl.radiusMul, 0.4f, 3.5f);
            candidates.push_back({nodeWorldXZ(iso, {x, y}), r, 0.0f});
        }
    }
    for (Site& site : candidates) {
        site.jitter = static_cast<float>(uni01(rng)); // stable tiebreak
    }
    std::sort(candidates.begin(), candidates.end(), [](const Site& a, const Site& b) {
        return a.radius != b.radius ? a.radius > b.radius : a.jitter > b.jitter;
    });

    // Greedy packing: biggest first; accept if far enough from everything
    // already accepted (overlap < 1 forces interpenetration).
    std::vector<Site> accepted;
    for (const Site& site : candidates) {
        if (static_cast<int>(accepted.size()) >= cl.maxLobes) {
            break;
        }
        bool ok = true;
        for (const Site& other : accepted) {
            const float dist = glm::length(site.xz - other.xz);
            if (dist <= cl.overlap * (site.radius + other.radius)) {
                ok = false;
                break;
            }
        }
        if (ok) {
            accepted.push_back(site);
        }
    }

    // Lobes.
    struct LobeRange {
        int beginVert = 0, endVert = 0; // vertex range in the merged mesh
        glm::vec3 center{0.0f};
        float sphereR = 0.0f;
    };
    std::vector<LobeRange> lobes;
    out.mesh.extentMin = glm::vec3(1e30f);
    out.mesh.extentMax = glm::vec3(-1e30f);
    for (std::size_t i = 0; i < accepted.size(); ++i) {
        const Site& site = accepted[i];
        StoneGenParams p = gen;
        p.seed = gen.seed + static_cast<int>(i) * 1000 + 17;
        p.radius = site.radius;
        p.height = site.radius * (0.9f + 0.5f * static_cast<float>(uni01(rng)));
        const float formU = static_cast<float>(uni01(rng));
        const float blocksU = static_cast<float>(uni01(rng));
        if (cl.mixForms) {
            if (site.radius < 0.9f && formU < 0.35f) {
                p.form = StoneBaseForm::Oval; // small round pebbles at the border
            } else if (formU < 0.5f) {
                p.form = StoneBaseForm::Frustum;
            } else if (formU < 0.75f) {
                p.form = StoneBaseForm::Box;
            } else {
                p.form = StoneBaseForm::Prism;
                p.sides = 5 + static_cast<int>(uni01(rng) * 3.0); // 5..7
            }
        }
        // Big monoliths are always stacks (the bare-top look otherwise);
        // pebbles stay simple.
        if (site.radius >= 1.0f) {
            p.blocks = blocksU < 0.45f ? 2 : 3;
        } else {
            p.blocks = blocksU < 0.7f ? 1 : 2;
        }

        const int beginVert = static_cast<int>(out.mesh.pos.size() / 3);
        StoneMesh lobe = generateLobe(p);

        // Merge in world space.
        const glm::dvec3 offset{site.xz.x, 0.0, site.xz.y};
        glm::dvec3 centroid{0.0};
        for (std::size_t v = 0; v < lobe.pos.size() / 3; ++v) {
            lobe.pos[v * 3] += static_cast<float>(offset.x);
            lobe.pos[v * 3 + 2] += static_cast<float>(offset.z);
            centroid += glm::dvec3{lobe.pos[v * 3], lobe.pos[v * 3 + 1], lobe.pos[v * 3 + 2]};
        }
        if (!lobe.pos.empty()) {
            centroid /= static_cast<double>(lobe.pos.size() / 3);
        }
        double sphereR = 0.0;
        for (std::size_t v = 0; v < lobe.pos.size() / 3; ++v) {
            sphereR = std::max(
                sphereR,
                glm::distance(centroid, glm::dvec3{lobe.pos[v * 3], lobe.pos[v * 3 + 1], lobe.pos[v * 3 + 2]}));
        }

        out.mesh.pos.insert(out.mesh.pos.end(), lobe.pos.begin(), lobe.pos.end());
        out.mesh.nrm.insert(out.mesh.nrm.end(), lobe.nrm.begin(), lobe.nrm.end());
        out.mesh.col.insert(out.mesh.col.end(), lobe.col.begin(), lobe.col.end());
        out.mesh.triCount += lobe.triCount;
        if (!lobe.pos.empty()) {
            out.mesh.extentMin = glm::min(out.mesh.extentMin, lobe.extentMin + glm::vec3(offset));
            out.mesh.extentMax = glm::max(out.mesh.extentMax, lobe.extentMax + glm::vec3(offset));
        }

        lobes.push_back({
            beginVert,
            static_cast<int>(out.mesh.pos.size() / 3),
            glm::vec3(centroid),
            static_cast<float>(sphereR * 0.8)});
        out.centers.push_back({site.xz.x, 0.0f, site.xz.y});
        out.radii.push_back(site.radius);
    }

    if (out.centers.empty()) {
        out.mesh.extentMin = glm::vec3(0.0f);
        out.mesh.extentMax = glm::vec3(0.0f);
    }

    // Contact AO: darken vertices near other lobes' bounding spheres.
    if (cl.aoStrength > 0.0f) {
        for (std::size_t i = 0; i < lobes.size(); ++i) {
            const LobeRange& self = lobes[i];
            for (int v = self.beginVert; v < self.endVert; ++v) {
                const glm::vec3 pv{out.mesh.pos[v * 3], out.mesh.pos[v * 3 + 1], out.mesh.pos[v * 3 + 2]};
                float occ = 0.0f;
                for (std::size_t j = 0; j < lobes.size(); ++j) {
                    if (j == i) {
                        continue;
                    }
                    const float influence = lobes[j].sphereR * (1.0f + cl.aoFalloff);
                    const float dist = glm::distance(pv, lobes[j].center);
                    if (dist < influence) {
                        occ += 1.0f - dist / influence;
                    }
                }
                const float factor = 1.0f - cl.aoStrength * std::min(occ, 1.0f);
                out.mesh.col[v * 4] *= factor;
                out.mesh.col[v * 4 + 1] *= factor;
                out.mesh.col[v * 4 + 2] *= factor;
            }
        }
    }

    return out;
}
