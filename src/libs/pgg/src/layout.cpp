#include "pch.h"

#include "layout.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pgg {
namespace {

// One `@pos X Y` match inside a comment: the coordinates and the span
// covering exactly the two integers (write-back replaces that span only).
struct PosHintMatch {
    int x = 0;
    int y = 0;
    size_t intsBegin = 0;  // first character of the first integer
    size_t intsEnd = 0;    // one past the last character of the second integer
};

bool isDigit(char c) { return c >= '0' && c <= '9'; }
bool isSpace(char c) { return c == ' ' || c == '\t'; }

// Parses one signed integer at `p` (advanced past it). Rejects anything over
// int32 range — such a "coordinate" is garbage, not a hint.
bool parseHintInt(const std::string& s, size_t& p, int& out) {
    while (p < s.size() && isSpace(s[p])) ++p;
    bool neg = false;
    if (p < s.size() && (s[p] == '+' || s[p] == '-')) {
        neg = s[p] == '-';
        ++p;
    }
    if (p >= s.size() || !isDigit(s[p])) return false;
    long long v = 0;
    while (p < s.size() && isDigit(s[p])) {
        v = v * 10 + (s[p] - '0');
        if (v > std::numeric_limits<int32_t>::max()) return false;
        ++p;
    }
    out = static_cast<int>(neg ? -v : v);
    return true;
}

bool findPosHint(const std::string& comment, PosHintMatch& m) {
    size_t pos = 0;
    while ((pos = comment.find("@pos", pos)) != std::string::npos) {
        size_t p = pos + 4;
        // Word boundary: `@position 1 2` is not a hint.
        if (p < comment.size() && isSpace(comment[p])) {
            const size_t intsBegin = [&] {
                size_t q = p;
                while (q < comment.size() && isSpace(comment[q])) ++q;
                return q;
            }();
            int x = 0, y = 0;
            size_t q = p;
            if (parseHintInt(comment, q, x) && parseHintInt(comment, q, y)) {
                // The second integer must terminate cleanly (end or spaces) —
                // `@pos 1 20px` is a dirty hint and ignored.
                if (q >= comment.size() || isSpace(comment[q])) {
                    m.x = x;
                    m.y = y;
                    m.intsBegin = intsBegin;
                    m.intsEnd = q;
                    return true;
                }
            }
        }
        pos += 4;
    }
    return false;
}

float nodeHeight(const GraphNode& n, const LayoutParams& params) {
    const size_t rows = std::max<size_t>({n.inputs.size(), n.outputs.size(), 1});
    return params.headerH + static_cast<float>(rows) * params.pinRowH;
}

// The layout units of one scope level: plain nodes of the level plus the
// frames of its direct child zones (a zone header belongs to its frame).
struct LevelUnits {
    struct Unit {
        int node = -1;  // plain node
        int zone = -1;  // or child-zone frame
        float w = 0.0f;
        float h = 0.0f;
        int layer = 0;
    };
    std::vector<Unit> units;
    std::unordered_map<int, int> byNode;  // node id -> unit index
    std::unordered_map<int, int> byZone;  // zone id -> unit index
};

class Layout {
public:
    Layout(GraphScope& g, const LayoutParams& params) : g_(g), params_(params) {
        for (const GraphZone& z : g_.zones) zoneOfHeader_[z.header] = z.id;
    }

    void run() {
        for (const GraphZone& z : g_.zones)
            if (z.parent == -1) layoutZone(z.id);
        layoutLevel(-1);
        // Absolutize: member coordinates are frame-local; adding the frame
        // origins top-down makes everything scope-absolute. Zone ids are
        // created parent-first by the graph walk, so ascending order applies
        // every parent offset before its children.
        for (GraphZone& z : g_.zones) {
            for (const int m : z.members) {
                g_.nodes[m].x += z.x;
                g_.nodes[m].y += z.y;
            }
            g_.nodes[z.header].x += z.x;
            g_.nodes[z.header].y += z.y;
            for (const int c : z.children) {
                g_.zones[c].x += z.x;
                g_.zones[c].y += z.y;
            }
        }
    }

private:
    GraphScope& g_;
    const LayoutParams& params_;
    std::unordered_map<int, int> zoneOfHeader_;  // header node id -> zone id

    // Maps a node to the unit representing it at `level` (-1 = scope root):
    // the node itself when it lives at the level, the top-most frame under
    // the level when it sits inside a child zone, -1 when it is outside the
    // level's subtree (that wire matters at an outer level instead).
    int unitAtLevel(const LevelUnits& lu, int nodeId, int level) const {
        const GraphNode& n = g_.nodes[nodeId];
        int z = n.zone;
        if (n.kind == GraphNode::Kind::ZoneHeader) {
            if (auto it = zoneOfHeader_.find(nodeId); it != zoneOfHeader_.end()) {
                z = it->second;  // the header rides its zone's frame
            }
        } else if (z == level) {
            if (auto it = lu.byNode.find(nodeId); it != lu.byNode.end()) return it->second;
            return -1;
        }
        while (z >= 0) {
            const int parent = g_.zones[z].parent;
            if (parent == level) {
                if (auto it = lu.byZone.find(z); it != lu.byZone.end()) return it->second;
                return -1;
            }
            z = parent;
        }
        return -1;
    }

    // Sugiyama-lite over the units of one level: longest-path layering
    // (feedback wires excluded by the caller), barycenter sweeps, grid
    // coordinates, hint override. Assigns frame-local coordinates.
    void layoutLevel(int level) {
        LevelUnits lu;
        for (GraphNode& n : g_.nodes) {
            if (n.zone != level || n.kind == GraphNode::Kind::ZoneHeader) continue;
            lu.byNode[n.id] = static_cast<int>(lu.units.size());
            lu.units.push_back(LevelUnits::Unit{n.id, -1, params_.nodeW, nodeHeight(n, params_), 0});
        }
        for (GraphZone& z : g_.zones) {
            if (z.parent != level) continue;
            lu.byZone[z.id] = static_cast<int>(lu.units.size());
            lu.units.push_back(LevelUnits::Unit{-1, z.id, z.w, z.h, 0});
        }
        if (lu.units.empty()) return;

        // Unit DAG from the scope's wires (state loops stay out of layering).
        std::vector<std::vector<int>> preds(lu.units.size()), succs(lu.units.size());
        for (const GraphEdge& e : g_.edges) {
            if (e.loop) continue;
            const int u = unitAtLevel(lu, e.fromNode, level);
            const int v = unitAtLevel(lu, e.toNode, level);
            if (u < 0 || v < 0 || u == v) continue;
            preds[v].push_back(u);
            succs[u].push_back(v);
        }

        // Longest path from the sources (the graph is a DAG at every level).
        std::vector<int> layer(lu.units.size(), -1);
        auto dfs = [&](auto&& self, int u) -> int {
            if (layer[u] >= 0) return layer[u];
            int best = 0;
            for (const int p : preds[u]) best = std::max(best, self(self, p) + 1);
            layer[u] = best;
            return best;
        };
        int layerCount = 0;
        for (size_t u = 0; u < lu.units.size(); ++u)
            layerCount = std::max(layerCount, dfs(dfs, static_cast<int>(u)) + 1);

        std::vector<std::vector<int>> rows(static_cast<size_t>(layerCount));
        for (size_t u = 0; u < lu.units.size(); ++u) rows[layer[u]].push_back(static_cast<int>(u));

        // Initial order: source position, ties by unit index (deterministic).
        auto spanOf = [&](int u) -> Span {
            const LevelUnits::Unit& unit = lu.units[u];
            if (unit.node >= 0) return g_.nodes[unit.node].span;
            return g_.nodes[g_.zones[unit.zone].header].span;
        };
        for (auto& r : rows) {
            std::stable_sort(r.begin(), r.end(), [&](int a, int b) {
                const Span sa = spanOf(a), sb = spanOf(b);
                if (sa.line != sb.line) return sa.line < sb.line;
                if (sa.col != sb.col) return sa.col < sb.col;
                return a < b;
            });
        }

        // Barycenter sweeps, alternating directions; ties keep the current
        // order (stable sort), so the result is deterministic.
        std::vector<int> rowOf(lu.units.size(), 0);
        auto reindex = [&] {
            for (size_t l = 0; l < rows.size(); ++l)
                for (size_t i = 0; i < rows[l].size(); ++i) rowOf[rows[l][i]] = static_cast<int>(i);
        };
        reindex();
        for (int pass = 0; pass < 4; ++pass) {
            const bool forward = pass % 2 == 0;
            const int begin = forward ? 1 : layerCount - 2;
            const int end = forward ? layerCount : -1;
            const int step = forward ? 1 : -1;
            for (int l = begin; l != end; l += step) {
                auto& r = rows[static_cast<size_t>(l)];
                const auto& neighbours = forward ? preds : succs;
                std::stable_sort(r.begin(), r.end(), [&](int a, int b) {
                    auto bary = [&](int u) -> float {
                        const auto& ns = neighbours[u];
                        if (ns.empty()) return static_cast<float>(rowOf[u]);
                        float sum = 0.0f;
                        for (const int n : ns) sum += static_cast<float>(rowOf[n]);
                        return sum / static_cast<float>(ns.size());
                    };
                    const float ba = bary(a), bb = bary(b);
                    if (ba != bb) return ba < bb;
                    return rowOf[a] < rowOf[b];
                });
                reindex();
            }
        }

        // Grid coordinates: columns per layer, rows stacked inside a layer.
        float x = 0.0f;
        for (int l = 0; l < layerCount; ++l) {
            auto& r = rows[static_cast<size_t>(l)];
            float colW = 0.0f;
            for (const int u : r) colW = std::max(colW, lu.units[u].w);
            float y = 0.0f;
            for (const int u : r) {
                LevelUnits::Unit& unit = lu.units[u];
                unit.layer = l;
                if (unit.node >= 0) {
                    GraphNode& n = g_.nodes[unit.node];
                    n.layer = l;
                    n.x = x + unit.w * 0.5f;
                    n.y = y + unit.h * 0.5f;
                    if (n.hasHint) {  // author intent wins (local overlaps are fine)
                        n.x = n.hintX;
                        n.y = n.hintY;
                    }
                } else {
                    GraphZone& z = g_.zones[unit.zone];
                    z.x = x;
                    z.y = y;
                }
                y += unit.h + params_.rowGapY;
            }
            x += colW + params_.layerGapX;
        }
    }

    // Recursive zone layout: the body is one level, the frame wraps it.
    void layoutZone(int zid) {
        GraphZone& z = g_.zones[zid];
        for (const int c : z.children) layoutZone(c);
        layoutLevel(zid);

        float minX = std::numeric_limits<float>::max();
        float minY = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float maxY = std::numeric_limits<float>::lowest();
        for (const int m : z.members) {
            const GraphNode& n = g_.nodes[m];
            const float hw = params_.nodeW * 0.5f;
            const float hh = nodeHeight(n, params_) * 0.5f;
            minX = std::min(minX, n.x - hw);
            minY = std::min(minY, n.y - hh);
            maxX = std::max(maxX, n.x + hw);
            maxY = std::max(maxY, n.y + hh);
        }
        for (const int c : z.children) {
            const GraphZone& cz = g_.zones[c];
            minX = std::min(minX, cz.x);
            minY = std::min(minY, cz.y);
            maxX = std::max(maxX, cz.x + cz.w);
            maxY = std::max(maxY, cz.y + cz.h);
        }
        if (minX > maxX) {  // no content at all (defensive; ports always exist)
            minX = minY = 0.0f;
            maxX = maxY = 0.0f;
        }
        const float dx = params_.zonePadX - minX;
        const float dy = params_.zonePadTop - minY;
        for (const int m : z.members) {
            g_.nodes[m].x += dx;
            g_.nodes[m].y += dy;
        }
        for (const int c : z.children) {
            g_.zones[c].x += dx;
            g_.zones[c].y += dy;
        }
        z.w = (maxX - minX) + 2.0f * params_.zonePadX;
        z.h = (maxY - minY) + params_.zonePadTop + params_.zonePadBottom;
        z.x = 0.0f;  // the parent level assigns the frame origin
        z.y = 0.0f;
        // The header rides the frame's title strip.
        GraphNode& header = g_.nodes[z.header];
        header.x = z.w * 0.5f;
        header.y = params_.zonePadTop * 0.5f;
    }
};

}  // namespace

bool parsePosHint(const std::string& comment, int& x, int& y) {
    PosHintMatch m;
    if (!findPosHint(comment, m)) return false;
    x = m.x;
    y = m.y;
    return true;
}

std::string applyPosHint(const std::string& text, int32_t line, int x, int y) {
    if (line < 1) return text;
    size_t begin = 0;
    int32_t cur = 1;
    while (cur < line) {
        const size_t nl = text.find('\n', begin);
        if (nl == std::string::npos) return text;  // no such line
        begin = nl + 1;
        ++cur;
    }
    size_t end = text.find('\n', begin);
    if (end == std::string::npos) end = text.size();
    size_t contentEnd = end;
    if (contentEnd > begin && text[contentEnd - 1] == '\r') --contentEnd;
    const std::string lineText = text.substr(begin, contentEnd - begin);

    std::string newLine;
    PosHintMatch m;
    if (findPosHint(lineText, m)) {
        // Replace only the coordinates; the rest of the comment (and any text
        // around the hint) stays byte-identical.
        newLine = lineText.substr(0, m.intsBegin) + std::to_string(x) + " " + std::to_string(y) +
                  lineText.substr(m.intsEnd);
    } else {
        newLine = lineText + "  # @pos " + std::to_string(x) + " " + std::to_string(y);
    }
    return text.substr(0, begin) + newLine + text.substr(contentEnd);
}

void layoutScope(GraphScope& scope, const LayoutParams& params) {
    Layout l(scope, params);
    l.run();
}

void layoutProject(GraphProject& project, const LayoutParams& params) {
    layoutScope(project.top, params);
    for (GraphScope& s : project.instanceScopes) layoutScope(s, params);
}

}  // namespace pgg
