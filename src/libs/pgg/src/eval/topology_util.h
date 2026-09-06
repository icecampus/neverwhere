#pragma once

// Shared helpers of the §8.3 topology nodes (delete/clip/extrude/inset/
// subdivide/...): column gathering and row-source based column building
// (copy / lerp / fresh rows), plus planar ear clipping.

#include <array>
#include <memory>
#include <vector>

#include "geometry.h"

namespace pgg::topo {

// Gathers every column of an AttrSet / GroupSet by `keep` (indices into the
// old domain); null in -> null out.
std::shared_ptr<const AttrSet> gatherAttrs(const AttrSet* src, const std::vector<int32_t>& keep);
std::shared_ptr<const GroupSet> gatherGroups(const GroupSet* src, const std::vector<int32_t>& keep);

struct RowSrc {
    int32_t a = -1, b = -1;
    float t = 0.0f;
    static RowSrc copy(int32_t i) { return {i, i, 0.0f}; }
    static RowSrc lerp(int32_t a, int32_t b, float t) { return {a, b, t}; }
    static RowSrc fresh() { return {-1, -1, 0.0f}; }
};

// Builds a column from row sources; `freshVec3` (a typed default for
// RowSrc::fresh on vec3 columns) may be null -> zero value.
ColumnData buildColumn(const ColumnData& src, const std::vector<RowSrc>& rows, const glm::vec3* freshVec3);

// Every column of `src` rebuilt from `rows`; normal-tagged "N" columns take
// `freshNormal` for fresh rows.
std::shared_ptr<const AttrSet> buildAttrs(const AttrSet* src, const std::vector<RowSrc>& rows,
                                          const glm::vec3* freshNormal);
std::shared_ptr<const GroupSet> buildGroups(const GroupSet* src, const std::vector<RowSrc>& rows);

// Weighted row source: an output row is the normalized weighted blend of
// input rows (numeric/vector columns), or the heaviest input row (int/bool/
// string, first on ties). Empty -> fresh row (typed default).
struct RowBlend {
    std::vector<std::pair<int32_t, float>> w;
    static RowBlend copy(int32_t i) { return RowBlend{{{i, 1.0f}}}; }
    static RowBlend average(const std::vector<int32_t>& idx) {
        RowBlend r;
        const float k = idx.empty() ? 0.0f : 1.0f / static_cast<float>(idx.size());
        for (int32_t i : idx) r.w.emplace_back(i, k);
        return r;
    }
};
ColumnData buildColumnBlend(const ColumnData& src, const std::vector<RowBlend>& rows, const glm::vec3* freshVec3);
std::shared_ptr<const AttrSet> buildAttrsBlend(const AttrSet* src, const std::vector<RowBlend>& rows,
                                               const glm::vec3* freshNormal);
std::shared_ptr<const GroupSet> buildGroupsBlend(const GroupSet* src, const std::vector<RowBlend>& rows);

// delete(geo, where, domain) core: removes the elements of `domain` flagged in
// `drop` (size = element count) with the §8.3 cascade. Caller validates the
// domain against the kind.
GeoPtr deleteByMask(const Geo& in, Domain domain, const std::vector<uint8_t>& drop);

// Ear clipping of a planar polygon given in 2D; appends triangle index triples
// into `pts`. Falls back to a fan when no ear is found (degenerate input).
void earClip(const std::vector<glm::vec2>& pts, std::vector<std::array<int, 3>>& tris);

}  // namespace pgg::topo
