#include "pch.h"

#include "walls.h"

#include <utility>

#include <FastNoise/FastNoise.h>

#include "boundary.h"

namespace highground {
namespace {

using MapSeg = ContourMapSeg;
using Chain = ContourChain;

constexpr float kPi = 3.14159265358979323846f;

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

Vertex wallVertex(const glm::vec2& fieldPos, const glm::vec3& color) {
    Vertex v;
    v.pos = fieldPos;
    v.uv = {0.0f, 0.0f};
    v.color = {color.r, color.g, color.b, 1.0f};
    return v;
}

} // namespace

WallBuild buildRockWalls(
    const std::vector<RockContourSegment>& segments,
    const Params& params,
    const topology_core::DiamondIsometry& iso) {

    WallBuild build;

    std::vector<MapSeg> segs;
    const std::vector<Chain> chains = buildContourChains(segments, segs);

    std::vector<WallPiece> pieces;
    for (const Chain& chain : chains) {
        BeveledBoundary beveled = bevelChain(segs, chain, params.bevel);
        pieces.insert(pieces.end(), beveled.pieces.begin(), beveled.pieces.end());
    }
    if (pieces.empty()) {
        return build;
    }

    const float heightPx = params.height;
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
        displaced[i] = s.base + s.normal * offset;
    }

    // Emit quads with baked flat colors (+ one depth key per quad: the ground
    // y of the quad's bottom edge = max y over its 6 final vertices).
    const auto sampleAt = [&](int piece, int i, int j) -> std::size_t {
        const std::size_t perPiece = static_cast<std::size_t>(hSub + 1) * static_cast<std::size_t>(vSub + 1);
        return static_cast<std::size_t>(piece) * perPiece +
            static_cast<std::size_t>(i) * static_cast<std::size_t>(vSub + 1) + static_cast<std::size_t>(j);
    };
    build.verts.reserve(pieces.size() * static_cast<std::size_t>(hSub) * static_cast<std::size_t>(vSub) * 6);
    build.depths.reserve(pieces.size() * static_cast<std::size_t>(hSub) * static_cast<std::size_t>(vSub));
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

                float depth = std::numeric_limits<float>::lowest();
                const auto emit = [&](std::size_t idx) {
                    const glm::vec2 f = mapToFieldPx(iso, displaced[idx]);
                    const float z = samples[idx].heightT * heightPx;
                    depth = std::max(depth, f.y - z);
                    Vertex v = wallVertex({f.x, f.y - z}, color);
                    v.groundY = f.y; // field y before the lift (z = heightT * height)
                    v.normal = samples[idx].normal; // unit outward, map space
                    build.verts.push_back(v);
                };
                emit(i00);
                emit(i10);
                emit(i11);
                emit(i00);
                emit(i11);
                emit(i01);
                build.depths.push_back(depth);
            }
        }
    }
    return build;
}

void appendFlatWalls(
    std::vector<Vertex>& verts,
    std::vector<float>& depths,
    const topology_core::DiamondIsometry& iso,
    const glm::ivec2& cell,
    const std::array<bool, 4>& mask,
    float heightPx) {

    // Map-space contour segments carry the unit outward normal (triplanar
    // blend weights); field positions come from the same map points.
    const auto segments = cellRockContourSegments(cell, mask);
    for (const RockContourSegment& seg : segments) {
        const glm::vec2 edgeMid = mapToFieldPx(iso, seg.a);
        const glm::vec2 center = mapToFieldPx(iso, seg.b);
        // Same two-level axis shading as before: segment direction in map
        // space picks the brightness level (axis 0: parallel to grid X).
        const glm::vec2 d = seg.b - seg.a;
        const glm::vec3 base = std::abs(d.x) >= std::abs(d.y)
            ? glm::vec3(0.62f, 0.45f, 0.22f)
            : glm::vec3(0.45f, 0.32f, 0.16f);
        const glm::vec4 top{base, 1.0f};
        const glm::vec4 bottom{base * 0.7f, 1.0f};

        const auto v = [&](const glm::vec2& p, float lift, const glm::vec4& c) {
            Vertex out;
            out.pos = {p.x, p.y - lift};
            out.uv = {0.0f, 0.0f};
            out.color = c;
            out.groundY = p.y;
            out.normal = seg.outward;
            return out;
        };
        const Vertex t0 = v(edgeMid, heightPx, top);
        const Vertex t1 = v(center, heightPx, top);
        const Vertex b0 = v(edgeMid, 0.0f, bottom);
        const Vertex b1 = v(center, 0.0f, bottom);

        verts.push_back(t0);
        verts.push_back(b0);
        verts.push_back(b1);
        verts.push_back(t0);
        verts.push_back(b1);
        verts.push_back(t1);

        depths.push_back(std::max(edgeMid.y, center.y));
    }
}

std::vector<std::vector<glm::vec2>> beveledLoops(
    const std::vector<RockContourSegment>& segments,
    float cornerBevel) {

    std::vector<std::vector<glm::vec2>> loops;
    std::vector<MapSeg> segs;
    const std::vector<Chain> chains = buildContourChains(segments, segs);
    for (const Chain& chain : chains) {
        // Same bevel pass as the wall builder: consecutive pieces share
        // endpoints, so the piece list doubles as the wall top boundary
        // polyline — collect the piece starts into a closed loop.
        const BeveledBoundary beveled = bevelChain(segs, chain, cornerBevel);
        if (beveled.pieces.empty()) {
            continue;
        }
        std::vector<glm::vec2> loop;
        loop.reserve(beveled.pieces.size() + 1);
        for (const WallPiece& piece : beveled.pieces) {
            loop.push_back(piece.a);
        }
        if (!chain.closed) {
            loop.push_back(beveled.pieces.back().b);
        }
        if (loop.size() >= 3) {
            loops.push_back(std::move(loop));
        }
    }
    return loops;
}

} // namespace highground
