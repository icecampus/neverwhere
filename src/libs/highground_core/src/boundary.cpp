#include "pch.h"

#include "boundary.h"

#include <map>

namespace highground {
namespace {

glm::ivec2 keyOf(const glm::vec2& p) {
    return {static_cast<int>(std::lround(p.x * 2.0f)), static_cast<int>(std::lround(p.y * 2.0f))};
}

struct KeyLess {
    bool operator()(const glm::ivec2& a, const glm::ivec2& b) const {
        return a.y != b.y ? a.y < b.y : a.x < b.x;
    }
};

using Adjacency = std::map<glm::ivec2, std::vector<int>, KeyLess>;

Adjacency buildAdjacency(const std::vector<ContourMapSeg>& segs) {
    Adjacency adj;
    for (int i = 0; i < static_cast<int>(segs.size()); ++i) {
        adj[segs[i].aKey].push_back(i);
        adj[segs[i].bKey].push_back(i);
    }
    return adj;
}

// Flip travel direction if needed so the outward normal sits on the right.
void orientForTravel(ContourMapSeg& seg) {
    const glm::vec2 dir = seg.b - seg.a;
    const glm::vec2 right(dir.y, -dir.x);
    if (glm::dot(right, seg.outward) < 0.0f) {
        std::swap(seg.a, seg.b);
        std::swap(seg.aKey, seg.bKey);
    }
}

} // namespace

std::vector<ContourChain> buildContourChains(
    const std::vector<RockContourSegment>& segments,
    std::vector<ContourMapSeg>& segs) {

    segs.clear();
    segs.reserve(segments.size());
    for (const RockContourSegment& seg : segments) {
        ContourMapSeg mapSeg;
        mapSeg.aKey = keyOf(seg.a);
        mapSeg.bKey = keyOf(seg.b);
        mapSeg.a = seg.a;
        mapSeg.b = seg.b;
        mapSeg.outward = seg.outward;
        segs.push_back(mapSeg);
    }

    const Adjacency adj = buildAdjacency(segs);
    std::vector<ContourChain> chains;

    for (int start = 0; start < static_cast<int>(segs.size()); ++start) {
        if (segs[start].visited) {
            continue;
        }
        ContourChain chain;
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

glm::vec2 mapToFieldPx(const topology_core::DiamondIsometry& iso, const glm::vec2& map) {
    const glm::vec2 cellSz = iso.dims.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;
    return {(map.x - map.y) * halfW + halfW, (map.x + map.y) * halfH};
}

std::vector<std::vector<glm::vec2>> boundaryLoops(const Grid& grid, float cellWidth, float cellHeight) {
    std::vector<std::vector<glm::vec2>> loops;
    if (grid.width <= 0 || grid.height <= 0) {
        return loops;
    }

    topology_core::DiamondIsometry iso;
    iso.dims.cellWidth = cellWidth;
    iso.dims.aspectRatio = cellWidth / cellHeight;

    // Land cells + corner-node masks (same enumeration as generate()).
    std::vector<RockContourSegment> segments;
    for (int cy = grid.originY - 1; cy <= grid.originY + grid.height; ++cy) {
        for (int cx = grid.originX - 1; cx <= grid.originX + grid.width; ++cx) {
            const auto corners = topology_core::DiamondIsometry::cellCornerNodes({cx, cy});
            const std::array<bool, 4> mask{
                grid.at(corners[0]),
                grid.at(corners[1]),
                grid.at(corners[2]),
                grid.at(corners[3]),
            };
            if (!(mask[0] || mask[1] || mask[2] || mask[3])) {
                continue;
            }
            std::vector<RockContourSegment> segs = cellRockContourSegments({cx, cy}, mask);
            segments.insert(segments.end(), segs.begin(), segs.end());
        }
    }
    if (segments.empty()) {
        return loops;
    }

    std::vector<ContourMapSeg> segs;
    const std::vector<ContourChain> chains = buildContourChains(segments, segs);

    // Ordered boundary polyline per closed chain, split into simple loops at
    // repeated (pinch) vertices, then collinear points dropped — the result
    // is a set of strictly-simple loops (the "simplified" boundary-first
    // contract: safe to extrude and to fill).
    for (const ContourChain& chain : chains) {
        if (!chain.closed || chain.segs.size() < 3) {
            continue;
        }
        std::vector<glm::vec2> stack;
        std::vector<glm::ivec2> stackKeys;
        const auto flushLoop = [&](std::size_t from) {
            if (stack.size() - from < 3) {
                return;
            }
            std::vector<glm::vec2> loop;
            loop.reserve(stack.size() - from);
            for (std::size_t i = from; i < stack.size(); ++i) {
                loop.push_back(mapToFieldPx(iso, stack[i]));
            }
            loops.push_back(std::move(loop));
        };
        for (const int si : chain.segs) {
            const glm::vec2& p = segs[si].a;
            const glm::ivec2 key = keyOf(p);
            const auto it = std::find(stackKeys.begin(), stackKeys.end(), key);
            if (it != stackKeys.end()) {
                // Second visit of a pinch point: the walk since its first
                // visit forms a closed simple loop — extract it and continue
                // the outer walk from the pinch point.
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

    // Collinear-point cleanup: drop vertices lying exactly on the segment
    // between their neighbors (no shape information).
    for (std::vector<glm::vec2>& loop : loops) {
        for (bool changed = true; changed && loop.size() > 3;) {
            changed = false;
            for (std::size_t i = 0; i < loop.size(); ++i) {
                const glm::vec2& a = loop[(i + loop.size() - 1) % loop.size()];
                const glm::vec2& b = loop[i];
                const glm::vec2& c = loop[(i + 1) % loop.size()];
                const glm::vec2 seg = c - a;
                const float len = glm::length(seg);
                const float cross = (b.x - a.x) * seg.y - (b.y - a.y) * seg.x;
                const float dist = len > 1e-6f ? std::abs(cross) / len : 0.0f;
                if (dist > 0.01f) {
                    continue;
                }
                if (b.x < std::min(a.x, c.x) - 0.01f || b.x > std::max(a.x, c.x) + 0.01f ||
                    b.y < std::min(a.y, c.y) - 0.01f || b.y > std::max(a.y, c.y) + 0.01f) {
                    continue;
                }
                loop.erase(loop.begin() + static_cast<std::ptrdiff_t>(i));
                changed = true;
                break;
            }
        }
    }
    loops.erase(
        std::remove_if(loops.begin(), loops.end(), [](const std::vector<glm::vec2>& loop) {
            return loop.size() < 3;
        }),
        loops.end());
    return loops;
}

} // namespace highground
