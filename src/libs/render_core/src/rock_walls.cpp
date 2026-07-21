#include "rock_walls.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <utility>

#include <FastNoise/FastNoise.h>

namespace render_core {
namespace {

constexpr float kPi = 3.14159265358979323846f;

// Field projection for fractional map coordinates — the same affine as
// DiamondIsometry::nodeToField (contour endpoints sit on half-integer coords).
glm::vec2 mapToFieldPx(const topology_core::DiamondIsometry& iso, const glm::vec2& map) {
    const glm::vec2 cellSz = iso.dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;
    return {(map.x - map.y) * halfW + halfW, (map.x + map.y) * halfH};
}

// ---------------------------------------------------------------------------
// Chains: closed polylines over shared endpoints, walked with land on the
// left (outward normals on the right of travel).
// ---------------------------------------------------------------------------

struct MapSeg {
    glm::ivec2 aKey{}, bKey{}; // half-grid endpoint keys (2 * map)
    glm::vec2 a{}, b{};        // map space endpoints (travel order)
    glm::vec2 outward{};       // unit outward normal (axial), map space
    bool visited = false;
};

glm::ivec2 keyOf(const glm::vec2& p) {
    return {static_cast<int>(std::lround(p.x * 2.0f)), static_cast<int>(std::lround(p.y * 2.0f))};
}

struct KeyLess {
    bool operator()(const glm::ivec2& a, const glm::ivec2& b) const {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    }
};

using Adjacency = std::map<glm::ivec2, std::vector<int>, KeyLess>;

Adjacency buildAdjacency(const std::vector<MapSeg>& segs) {
    Adjacency adj;
    for (int i = 0; i < static_cast<int>(segs.size()); ++i) {
        adj[segs[i].aKey].push_back(i);
        adj[segs[i].bKey].push_back(i);
    }
    return adj;
}

// Flip travel direction if needed so the outward normal sits on the right.
void orientForTravel(MapSeg& seg) {
    const glm::vec2 dir = seg.b - seg.a;
    const glm::vec2 right(dir.y, -dir.x);
    if (glm::dot(right, seg.outward) < 0.0f) {
        std::swap(seg.a, seg.b);
        std::swap(seg.aKey, seg.bKey);
    }
}

struct Chain {
    std::vector<int> segs; // indices into the shared segment array, travel order
    bool closed = false;
    int diagonalJoins = 0;
};

std::vector<Chain> buildChains(std::vector<MapSeg>& segs) {
    const Adjacency adj = buildAdjacency(segs);
    std::vector<Chain> chains;

    for (int start = 0; start < static_cast<int>(segs.size()); ++start) {
        if (segs[start].visited) {
            continue;
        }
        Chain chain;
        orientForTravel(segs[start]);
        segs[start].visited = true;
        chain.segs.push_back(start);
        const glm::ivec2 startKey = segs[start].aKey;

        int current = start;
        for (int guard = 0; guard < 100000; ++guard) {
            const glm::ivec2 at = segs[current].bKey;
            if (at == startKey) {
                chain.closed = true;
                break;
            }
            auto it = adj.find(at);
            if (it == adj.end()) {
                break;
            }
            const glm::vec2 dirIn = glm::normalize(segs[current].b - segs[current].a);
            int next = -1;
            float best = -2.0f;
            int nextInconsistent = -1;
            float bestInconsistent = -2.0f;
            for (const int cand : it->second) {
                if (segs[cand].visited) {
                    continue;
                }
                // direction the candidate travels when LEAVING `at`
                const glm::vec2 dirOut = (segs[cand].aKey == at)
                    ? (segs[cand].b - segs[cand].a)
                    : (segs[cand].a - segs[cand].b);
                const float score = glm::dot(glm::normalize(dirOut), dirIn);
                // Land-on-left consistency: the segment's outward normal must
                // sit on the right of travel. At a 4-way (diagonal) join the
                // straightest continuation crosses the pinch into the
                // neighbouring loop and walks it BACKWARDS (mixed winding);
                // prefer consistent candidates, keep the old greedy as a
                // degenerate fallback.
                const glm::vec2 rightOfTravel(dirOut.y, -dirOut.x);
                const bool consistent = glm::dot(rightOfTravel, segs[cand].outward) > 0.0f;
                if (consistent) {
                    if (score > best) {
                        best = score;
                        next = cand;
                    }
                } else if (score > bestInconsistent) {
                    bestInconsistent = score;
                    nextInconsistent = cand;
                }
            }
            if (next < 0) {
                next = nextInconsistent;
            }
            if (next < 0) {
                break; // dead end (should not happen on a closed contour)
            }
            if (it->second.size() >= 4) {
                chain.diagonalJoins++;
            }
            // make travel leave `at`; on a consistent contour this also puts
            // the outward normal on the right of travel automatically.
            if (segs[next].aKey != at) {
                std::swap(segs[next].a, segs[next].b);
                std::swap(segs[next].aKey, segs[next].bKey);
            }
            segs[next].visited = true;
            chain.segs.push_back(next);
            current = next;
        }
        chains.push_back(std::move(chain));
    }
    return chains;
}

// ---------------------------------------------------------------------------
// Bevel: trim convex corners, insert 45-degree chamfer pieces.
// ---------------------------------------------------------------------------

struct WallPiece {
    glm::vec2 a{}, b{};   // map endpoints
    glm::vec2 na{}, nb{}; // per-column outward normals
    float length = 0.0f;
};

struct BeveledBoundary {
    std::vector<WallPiece> pieces;
    int convexCorners = 0;
    int concaveCorners = 0;
};

BeveledBoundary bevelChain(const std::vector<MapSeg>& segs, const Chain& chain, float cornerBevel) {
    BeveledBoundary out;
    const int n = static_cast<int>(chain.segs.size());
    if (n == 0) {
        return out;
    }

    std::vector<float> trimStart(n, 0.0f), trimEnd(n, 0.0f);
    std::vector<glm::vec2> dirs(n);
    std::vector<float> lens(n);
    for (int i = 0; i < n; ++i) {
        const MapSeg& s = segs[chain.segs[i]];
        dirs[i] = glm::normalize(s.b - s.a);
        lens[i] = glm::length(s.b - s.a);
    }

    // Corner classification + trims (convex corners get a 45-degree chamfer).
    for (int i = 0; i < n; ++i) {
        const int prev = (i + n - 1) % n;
        if (!chain.closed && i == 0) {
            continue;
        }
        const float turn = dirs[prev].x * dirs[i].y - dirs[prev].y * dirs[i].x;
        if (turn > 1e-4f) {
            out.convexCorners++;
        } else if (turn < -1e-4f) {
            out.concaveCorners++;
        }
        if (turn <= 1e-4f) {
            continue; // straight or concave — no chamfer
        }
        trimEnd[prev] = std::min(cornerBevel, lens[prev] * 0.45f);
        trimStart[i] = std::min(cornerBevel, lens[i] * 0.45f);
    }

    // Ordered emission along the chain: main piece, then the chamfer to the
    // next piece. Consecutive pieces share endpoints, so the piece list
    // doubles as the wall top boundary polyline.
    for (int i = 0; i < n; ++i) {
        const MapSeg& s = segs[chain.segs[i]];
        WallPiece piece;
        piece.a = s.a + dirs[i] * trimStart[i];
        piece.b = s.b - dirs[i] * trimEnd[i];
        piece.na = s.outward;
        piece.nb = s.outward;
        piece.length = glm::length(piece.b - piece.a);
        if (piece.length > 1e-5f) {
            out.pieces.push_back(piece);
        }

        const int next = (i + 1) % n;
        if (!chain.closed && next == 0) {
            continue;
        }
        if (trimEnd[i] <= 0.0f && trimStart[next] <= 0.0f) {
            continue; // no convex corner between pieces i and next
        }
        const MapSeg& sn = segs[chain.segs[next]];
        WallPiece bevel;
        bevel.a = s.b - dirs[i] * trimEnd[i];
        bevel.b = sn.a + dirs[next] * trimStart[next];
        bevel.na = s.outward;
        bevel.nb = sn.outward;
        bevel.length = glm::length(bevel.b - bevel.a);
        if (bevel.length > 1e-5f) {
            out.pieces.push_back(bevel);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Noise / displacement / colors — block-cliff recipe.
// ---------------------------------------------------------------------------

FastNoise::SmartNode<> makeNoiseNode(float noiseScale) {
    auto simplex = FastNoise::New<FastNoise::Simplex>();
    simplex->SetScale(noiseScale);
    auto fractal = FastNoise::New<FastNoise::FractalRidged>();
    fractal->SetSource(simplex);
    fractal->SetOctaveCount(4);
    fractal->SetLacunarity(2.15f);
    fractal->SetGain(0.55f);
    fractal->SetWeightedStrength(0.35f);
    return fractal;
}

float clamp01(float v) {
    return std::clamp(v, 0.0f, 1.0f);
}

float terraceValue(float value, int steps) {
    if (steps <= 1) {
        return value;
    }
    const float normalized = clamp01((value + 1.0f) * 0.5f);
    const float terraced = std::floor(normalized * static_cast<float>(steps)) / static_cast<float>(steps);
    return terraced * 2.0f - 1.0f;
}

// Single-level seam envelope: zero offset at the top and bottom edges.
float seamFade(float heightT) {
    return std::sin(clamp01(heightT) * kPi);
}

glm::vec3 wallColor(float sideBias, float heightT, float noiseValue) {
    const float shade = noiseValue * 24.0f + (1.0f - heightT) * 12.0f + sideBias;
    return {
        std::clamp(92.0f + shade, 46.0f, 170.0f) / 255.0f,
        std::clamp(86.0f + shade, 44.0f, 160.0f) / 255.0f,
        std::clamp(78.0f + shade, 40.0f, 150.0f) / 255.0f,
    };
}

} // namespace

std::vector<RockContourSegment> cellRockContourSegments(
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask) {

    static constexpr int kEdges[4][2] = {{0, 1}, {1, 2}, {2, 3}, {3, 0}};
    const auto nodes = topology_core::DiamondIsometry::cellCornerNodes(cell);
    const glm::vec2 corners[4] = {
        glm::vec2(nodes[0]), // Left
        glm::vec2(nodes[1]), // Up
        glm::vec2(nodes[2]), // Right
        glm::vec2(nodes[3]), // Down
    };
    const glm::vec2 center(static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y) + 0.5f);

    std::vector<RockContourSegment> out;
    out.reserve(4);
    for (int e = 0; e < 4; ++e) {
        const int a = kEdges[e][0];
        const int b = kEdges[e][1];
        if (mask[a] == mask[b]) {
            continue; // both on (interior) or both off (no land) — no contour
        }
        const glm::vec2 mid = (corners[a] + corners[b]) * 0.5f;
        const glm::vec2 onCorner = mask[a] ? corners[a] : corners[b];
        const glm::vec2 quadCenter = (center + onCorner) * 0.5f;
        const glm::vec2 segDir = center - mid;
        glm::vec2 n(-segDir.y, segDir.x);
        if (glm::dot(n, mid - quadCenter) < 0.0f) {
            n = -n;
        }
        RockContourSegment seg;
        seg.a = mid;
        seg.b = center;
        seg.outward = glm::normalize(n);
        out.push_back(seg);
    }
    return out;
}

RockWallBuild buildRockWalls(
    const std::vector<RockContourSegment>& segments,
    float heightPx,
    const RockWallParams& params,
    const topology_core::DiamondIsometry& iso) {

    RockWallBuild build;

    std::vector<MapSeg> segs;
    segs.reserve(segments.size());
    for (const RockContourSegment& seg : segments) {
        MapSeg mapSeg;
        mapSeg.aKey = keyOf(seg.a);
        mapSeg.bKey = keyOf(seg.b);
        mapSeg.a = seg.a;
        mapSeg.b = seg.b;
        mapSeg.outward = seg.outward;
        segs.push_back(mapSeg);
    }

    std::vector<Chain> chains = buildChains(segs);

    std::vector<WallPiece> pieces;
    for (const Chain& chain : chains) {
        BeveledBoundary beveled = bevelChain(segs, chain, params.cornerBevel);
        // Wall top boundary polyline(s) in field space (unlifted) — exactly the
        // walls' upper contour. At diagonal joins the chain walk crosses the
        // pinch point twice (figure-eight), which a plain ear clipper cannot
        // digest, so the polyline is split into simple loops at repeated
        // vertices: the union of the loops is exactly the bounded region.
        if (chain.closed && beveled.pieces.size() >= 3) {
            std::vector<glm::vec2> stack;     // map space
            std::vector<glm::ivec2> stackKeys; // half-grid keys of `stack`
            const auto flushLoop = [&](std::size_t from) {
                if (stack.size() - from < 3) {
                    return;
                }
                std::vector<glm::vec2> loop;
                loop.reserve(stack.size() - from);
                for (std::size_t i = from; i < stack.size(); ++i) {
                    loop.push_back(mapToFieldPx(iso, stack[i]));
                }
                build.topChains.push_back(std::move(loop));
            };
            for (const WallPiece& piece : beveled.pieces) {
                const glm::vec2& p = piece.a;
                const glm::ivec2 key = keyOf(p);
                const auto it = std::find(stackKeys.begin(), stackKeys.end(), key);
                if (it != stackKeys.end()) {
                    // Second visit of a pinch point: the walk since its first
                    // visit forms a closed simple loop — extract it and
                    // continue the outer walk from the pinch point.
                    const std::size_t idx = static_cast<std::size_t>(it - stackKeys.begin());
                    flushLoop(idx);
                    stack.resize(idx + 1);
                    stackKeys.resize(idx + 1);
                } else {
                    stack.push_back(p);
                    stackKeys.push_back(key);
                }
            }
            flushLoop(0);
        }
        pieces.insert(pieces.end(), beveled.pieces.begin(), beveled.pieces.end());
    }
    if (pieces.empty()) {
        return build;
    }

    const float heightMap = heightPx / iso.dims.cellSize().y; // cell height = 1 unit
    const int hSub = std::max(1, params.hSub);
    const int vSub = std::max(1, params.vSub);

    // Grid samples over all pieces.
    struct Sample {
        glm::vec2 base;
        glm::vec2 normal;
        float heightT;
        float hSpacing;
        int piece;
    };
    std::vector<Sample> samples;
    samples.reserve(pieces.size() * static_cast<std::size_t>(hSub + 1) * static_cast<std::size_t>(vSub + 1));
    for (int p = 0; p < static_cast<int>(pieces.size()); ++p) {
        const WallPiece& piece = pieces[p];
        for (int i = 0; i <= hSub; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(hSub);
            const glm::vec2 base = piece.a + (piece.b - piece.a) * t;
            glm::vec2 n = piece.na + (piece.nb - piece.na) * t;
            const float len = glm::length(n);
            n = len > 1e-6f ? n / len : piece.na;
            for (int j = 0; j <= vSub; ++j) {
                Sample s;
                s.base = base;
                s.normal = n;
                s.heightT = 1.0f - static_cast<float>(j) / static_cast<float>(vSub);
                s.hSpacing = piece.length / static_cast<float>(hSub);
                s.piece = p;
                samples.push_back(s);
            }
        }
    }

    // Batch noise (world-coherent: seams match across pieces).
    std::vector<float> xs(samples.size()), ys(samples.size()), zs(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i) {
        xs[i] = samples[i].base.x;
        ys[i] = samples[i].base.y;
        zs[i] = samples[i].heightT * heightMap;
    }
    std::vector<float> fields(samples.size(), 0.0f);
    {
        auto node = makeNoiseNode(params.noiseScale);
        node->GenPositionArray3D(
            fields.data(), static_cast<int>(samples.size()), xs.data(), ys.data(), zs.data(),
            0.0f, 0.0f, 0.0f, params.seed);
    }
    for (float& f : fields) {
        f = std::clamp(f, -1.0f, 1.0f);
    }

    // Displaced grid vertices (map space) + offsets.
    const float vSpacing = heightMap / static_cast<float>(vSub);
    std::vector<glm::vec2> displaced(samples.size());
    std::vector<float> offsets(samples.size(), 0.0f);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        const Sample& s = samples[i];
        const float stepped = terraceValue(fields[i], params.terraceSteps);
        float offset = stepped * params.amplitude * seamFade(s.heightT);
        // Anti-fold clamp. Production uses 0.7*min(hSpacing,vSpacing); our
        // contour pieces are ~0.5 cell (shorter than production's 1.0), and
        // terrace quantization makes adjacent columns share offsets, so a
        // looser factor keeps the relief visible without self-intersection.
        const float maxOff = 1.2f * std::min(s.hSpacing, vSpacing);
        offset = std::clamp(offset, -maxOff, maxOff);
        offsets[i] = offset;
        build.maxAbsOffset = std::max(build.maxAbsOffset, std::abs(offset));
        displaced[i] = s.base + s.normal * offset;
    }

    // Emit quads with baked flat colors.
    const auto sampleAt = [&](int piece, int i, int j) -> std::size_t {
        const std::size_t perPiece = static_cast<std::size_t>(hSub + 1) * static_cast<std::size_t>(vSub + 1);
        return static_cast<std::size_t>(piece) * perPiece +
            static_cast<std::size_t>(i) * static_cast<std::size_t>(vSub + 1) + static_cast<std::size_t>(j);
    };
    build.verts.reserve(pieces.size() * static_cast<std::size_t>(hSub) * static_cast<std::size_t>(vSub) * 6);
    for (int p = 0; p < static_cast<int>(pieces.size()); ++p) {
        const WallPiece& piece = pieces[p];
        const glm::vec2 dominant = piece.na + piece.nb;
        const float sideBias = dominant.y > 0.5f ? 16.0f : (dominant.x > 0.5f ? -8.0f : 0.0f);
        for (int i = 0; i < hSub; ++i) {
            for (int j = 0; j < vSub; ++j) {
                const std::size_t i00 = sampleAt(p, i, j);
                const std::size_t i10 = sampleAt(p, i + 1, j);
                const std::size_t i11 = sampleAt(p, i + 1, j + 1);
                const std::size_t i01 = sampleAt(p, i, j + 1);
                const float heightMid = 1.0f - (static_cast<float>(j) + 0.5f) / static_cast<float>(vSub);
                const float fieldAvg = (fields[i00] + fields[i10] + fields[i11] + fields[i01]) * 0.25f;
                const glm::vec3 color = wallColor(sideBias, heightMid, fieldAvg);

                const auto emit = [&](std::size_t idx) {
                    const glm::vec2 f = mapToFieldPx(iso, displaced[idx]);
                    const float z = samples[idx].heightT * heightPx;
                    build.verts.push_back({f.x, f.y - z, color.r, color.g, color.b, 1.0f});
                };
                emit(i00);
                emit(i10);
                emit(i11);
                emit(i00);
                emit(i11);
                emit(i01);
                build.quadCount++;
            }
        }
    }
    return build;
}

} // namespace render_core
