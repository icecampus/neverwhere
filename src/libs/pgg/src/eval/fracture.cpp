#include "../../pch.h"

#include "fracture.h"

#include <numeric>
#include <unordered_map>

namespace pgg {
namespace {

struct UnionFind {
    std::vector<int32_t> parent;
    explicit UnionFind(size_t n) : parent(n) { std::iota(parent.begin(), parent.end(), 0); }
    int32_t find(int32_t x) {
        while (parent[static_cast<size_t>(x)] != x) {
            parent[static_cast<size_t>(x)] = parent[static_cast<size_t>(parent[static_cast<size_t>(x)])];
            x = parent[static_cast<size_t>(x)];
        }
        return x;
    }
    void unite(int32_t a, int32_t b) {
        a = find(a);
        b = find(b);
        if (a != b) parent[static_cast<size_t>(b)] = a;
    }
};

std::shared_ptr<const AttrSet> gatheredAttrs(const Geo& mesh, Domain d, const std::vector<int32_t>& idx) {
    const AttrSet* src = mesh.attrs(d);
    if (!src) return nullptr;
    AttrSet out;
    for (const auto& [name, col] : src->columns) out.columns[name] = AttrColumn{gatherColumn(col.data, idx), col.typeInfo};
    return std::make_shared<const AttrSet>(std::move(out));
}

std::shared_ptr<const GroupSet> gatheredGroups(const Geo& mesh, Domain d, const std::vector<int32_t>& idx) {
    const GroupSet* src = mesh.groups(d);
    if (!src) return nullptr;
    GroupSet out;
    for (const auto& [name, col] : src->columns)
        out.columns[name] = std::get<ConstBoolColumnPtr>(gatherColumn(ColumnData(col), idx));
    return std::make_shared<const GroupSet>(std::move(out));
}

// Column names across pieces in first-appearance order (piece order wins).
std::vector<std::string> unionNames(const std::vector<GeoPtr>& pieces, Domain d, bool groups) {
    std::vector<std::string> names;
    for (const GeoPtr& p : pieces) {
        if (groups) {
            const GroupSet* s = p->groups(d);
            if (!s) continue;
            for (const auto& [n, c] : s->columns)
                if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
        } else {
            const AttrSet* s = p->attrs(d);
            if (!s) continue;
            for (const auto& [n, c] : s->columns)
                if (std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
        }
    }
    return names;
}

}  // namespace

std::vector<int32_t> computeIslands(const Geo& mesh, size_t& outCount) {
    outCount = 0;
    std::vector<int32_t> ids(mesh.faceCount(), -1);
    if (mesh.faceCount() == 0) return ids;

    // Tagged pieces: an int @island_id on the faces groups faces by value.
    const AttrSet* fa = mesh.attrs(Domain::Faces);
    const AttrColumn* tag = fa ? fa->find("island_id") : nullptr;
    if (tag && std::holds_alternative<std::shared_ptr<const std::vector<int64_t>>>(tag->data)) {
        const auto& col = *std::get<std::shared_ptr<const std::vector<int64_t>>>(tag->data);
        if (col.size() == mesh.faceCount()) {
            std::unordered_map<int64_t, int32_t> dense;
            for (size_t f = 0; f < col.size(); ++f) {
                // First appearance while scanning faces ascending = ascending
                // minimum face index of the component.
                auto [it, fresh] = dense.try_emplace(col[f], static_cast<int32_t>(dense.size()));
                ids[f] = it->second;
                (void)fresh;
            }
            outCount = dense.size();
            return ids;
        }
    }

    if (!mesh.cornerVerts || !mesh.faceOffsets || mesh.pointCount() == 0) {
        // Degenerate mesh: every face is its own piece.
        outCount = mesh.faceCount();
        std::iota(ids.begin(), ids.end(), 0);
        return ids;
    }

    UnionFind uf(mesh.pointCount());
    for (size_t f = 0; f < mesh.faceCount(); ++f) {
        const int32_t begin = (*mesh.faceOffsets)[f];
        const int32_t end = (*mesh.faceOffsets)[f + 1];
        for (int32_t c = begin + 1; c < end; ++c)
            uf.unite((*mesh.cornerVerts)[static_cast<size_t>(begin)], (*mesh.cornerVerts)[static_cast<size_t>(c)]);
    }
    std::unordered_map<int32_t, int32_t> dense;
    for (size_t f = 0; f < mesh.faceCount(); ++f) {
        const int32_t root = uf.find((*mesh.cornerVerts)[static_cast<size_t>((*mesh.faceOffsets)[f])]);
        auto [it, fresh] = dense.try_emplace(root, static_cast<int32_t>(dense.size()));
        ids[f] = it->second;
        (void)fresh;
    }
    outCount = dense.size();
    return ids;
}

std::vector<GeoPtr> splitMeshPieces(const Geo& mesh) {
    size_t count = 0;
    const std::vector<int32_t> ids = computeIslands(mesh, count);
    std::vector<GeoPtr> pieces;
    if (count == 0) return pieces;

    std::vector<std::vector<int32_t>> faces(count);
    for (size_t f = 0; f < ids.size(); ++f) faces[static_cast<size_t>(ids[f])].push_back(static_cast<int32_t>(f));

    pieces.reserve(count);
    for (const std::vector<int32_t>& pieceFaces : faces) {
        std::vector<int32_t> cornerIdx;
        std::vector<int32_t> faceOffsets(1, 0);
        for (const int32_t f : pieceFaces) {
            const int32_t begin = (*mesh.faceOffsets)[static_cast<size_t>(f)];
            const int32_t end = (*mesh.faceOffsets)[static_cast<size_t>(f) + 1];
            for (int32_t c = begin; c < end; ++c) cornerIdx.push_back(c);
            faceOffsets.push_back(faceOffsets.back() + (end - begin));
        }
        // Compact point reindex in first-appearance (corner) order.
        std::vector<int32_t> remap(mesh.pointCount(), -1);
        std::vector<int32_t> pointIdx;
        std::vector<int32_t> cornerVerts(cornerIdx.size());
        for (size_t i = 0; i < cornerIdx.size(); ++i) {
            const int32_t p = (*mesh.cornerVerts)[static_cast<size_t>(cornerIdx[i])];
            if (remap[static_cast<size_t>(p)] < 0) {
                remap[static_cast<size_t>(p)] = static_cast<int32_t>(pointIdx.size());
                pointIdx.push_back(p);
            }
            cornerVerts[i] = remap[static_cast<size_t>(p)];
        }

        Geo out;
        out.kind = GeoKind::Mesh;
        out.positions = std::get<std::shared_ptr<const std::vector<glm::vec3>>>(
            gatherColumn(ColumnData(mesh.positions), pointIdx));
        if (mesh.normals)
            out.normals = std::get<std::shared_ptr<const std::vector<glm::vec3>>>(
                gatherColumn(ColumnData(mesh.normals), pointIdx));
        out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(cornerVerts));
        out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(faceOffsets));
        out.pointAttrs = gatheredAttrs(mesh, Domain::Points, pointIdx);
        out.cornerAttrs = gatheredAttrs(mesh, Domain::Corners, cornerIdx);
        out.faceAttrs = gatheredAttrs(mesh, Domain::Faces, pieceFaces);
        out.detailAttrs = mesh.detailAttrs;  // shared by pointer (§5.4)
        out.pointGroups = gatheredGroups(mesh, Domain::Points, pointIdx);
        out.cornerGroups = gatheredGroups(mesh, Domain::Corners, cornerIdx);
        out.faceGroups = gatheredGroups(mesh, Domain::Faces, pieceFaces);
        out.detailGroups = mesh.detailGroups;
        pieces.push_back(std::make_shared<const Geo>(std::move(out)));
    }
    return pieces;
}

std::vector<GeoPtr> splitPointPieces(const Geo& points) {
    std::vector<GeoPtr> pieces;
    if (!points.positions) return pieces;
    const size_t n = points.positions->size();
    pieces.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const std::vector<int32_t> idx{static_cast<int32_t>(i)};
        Geo out;
        out.kind = GeoKind::Points;
        out.positions = std::get<std::shared_ptr<const std::vector<glm::vec3>>>(
            gatherColumn(ColumnData(points.positions), idx));
        if (points.normals && points.normals->size() == n)
            out.normals = std::get<std::shared_ptr<const std::vector<glm::vec3>>>(
                gatherColumn(ColumnData(points.normals), idx));
        out.pointAttrs = gatheredAttrs(points, Domain::Points, idx);
        out.pointGroups = gatheredGroups(points, Domain::Points, idx);
        out.detailAttrs = points.detailAttrs;  // shared by pointer (§5.4)
        out.detailGroups = points.detailGroups;
        pieces.push_back(std::make_shared<const Geo>(std::move(out)));
    }
    return pieces;
}

GeoPtr mergeMeshPieces(const std::vector<GeoPtr>& pieces) {
    Geo out;
    bool allPoints = !pieces.empty();
    for (const GeoPtr& p : pieces) allPoints = allPoints && p->kind == GeoKind::Points;
    out.kind = allPoints ? GeoKind::Points : GeoKind::Mesh;
    {
        std::vector<glm::vec3> pos;
        std::vector<int32_t> corners;
        std::vector<int32_t> offsets(1, 0);
        for (const GeoPtr& p : pieces) {
            const int32_t pointShift = static_cast<int32_t>(pos.size());
            const int32_t cornerBase = static_cast<int32_t>(corners.size());
            if (p->positions) pos.insert(pos.end(), p->positions->begin(), p->positions->end());
            if (p->cornerVerts)
                for (const int32_t v : *p->cornerVerts) corners.push_back(v + pointShift);
            if (p->faceOffsets)
                for (size_t i = 1; i < p->faceOffsets->size(); ++i)
                    offsets.push_back((*p->faceOffsets)[i] + cornerBase);
        }
        out.positions = std::make_shared<const std::vector<glm::vec3>>(std::move(pos));
        if (!allPoints) {
            out.cornerVerts = std::make_shared<const std::vector<int32_t>>(std::move(corners));
            out.faceOffsets = std::make_shared<const std::vector<int32_t>>(std::move(offsets));
        }
    }
    // Normals: concat when any piece has them (zero-fill the rest).
    {
        bool any = false;
        for (const GeoPtr& p : pieces) any = any || p->normals != nullptr;
        if (any) {
            std::vector<glm::vec3> ns;
            for (const GeoPtr& p : pieces) {
                if (p->normals) ns.insert(ns.end(), p->normals->begin(), p->normals->end());
                else ns.resize(ns.size() + p->pointCount(), glm::vec3(0.0f));
            }
            out.normals = std::make_shared<const std::vector<glm::vec3>>(std::move(ns));
        }
    }
    for (Domain d : {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail}) {
        // Detail is a single element: union of names, the leftmost piece wins.
        if (d == Domain::Detail) {
            AttrSet attrs;
            for (const std::string& n : unionNames(pieces, d, false))
                for (const GeoPtr& p : pieces)
                    if (const AttrSet* s = p->attrs(d))
                        if (const AttrColumn* c = s->find(n)) {
                            attrs.columns[n] = *c;
                            break;
                        }
            if (!attrs.columns.empty()) out.detailAttrs = std::make_shared<const AttrSet>(std::move(attrs));
            GroupSet groups;
            for (const std::string& n : unionNames(pieces, d, true))
                for (const GeoPtr& p : pieces)
                    if (const GroupSet* s = p->groups(d))
                        if (ConstBoolColumnPtr c = s->find(n)) {
                            groups.columns[n] = std::move(c);
                            break;
                        }
            if (!groups.columns.empty()) out.detailGroups = std::make_shared<const GroupSet>(std::move(groups));
            continue;
        }
        AttrSet attrs;
        for (const std::string& n : unionNames(pieces, d, false)) {
            // Exemplar column (first piece that has the name) fixes the type and
            // tag; pieces before it are neutral-filled too, so the column
            // length matches the merged domain regardless of which piece
            // carries the name first.
            const AttrColumn* exemplar = nullptr;
            for (const GeoPtr& p : pieces) {
                const AttrSet* s = p->attrs(d);
                if (const AttrColumn* c = s ? s->find(n) : nullptr) { exemplar = c; break; }
            }
            if (!exemplar) continue;
            ColumnData acc;
            bool have = false;
            for (const GeoPtr& p : pieces) {
                const AttrSet* s = p->attrs(d);
                const AttrColumn* c = s ? s->find(n) : nullptr;
                ColumnData part = c ? convertColumn(c->data, exemplar->data)
                                    : neutralColumnLike(n, *exemplar, *p, d, p->elementCount(d));
                acc = have ? concatColumns(acc, part) : part;
                have = true;
            }
            attrs.columns[n] = AttrColumn{std::move(acc), exemplar->typeInfo};
        }
        GroupSet groups;
        for (const std::string& n : unionNames(pieces, d, true)) {
            ColumnData acc;
            bool have = false;
            for (const GeoPtr& p : pieces) {
                const GroupSet* s = p->groups(d);
                ConstBoolColumnPtr c = s ? s->find(n) : nullptr;
                if (!have) {
                    if (!c) continue;
                    acc = ColumnData(c);
                    have = true;
                    continue;
                }
                acc = concatColumns(acc, c ? ColumnData(c) : zeroColumnLike(acc, p->elementCount(d)));
            }
            if (have) groups.columns[n] = std::get<ConstBoolColumnPtr>(std::move(acc));
        }
        std::shared_ptr<const AttrSet> attrPtr =
            attrs.columns.empty() ? nullptr : std::make_shared<const AttrSet>(std::move(attrs));
        std::shared_ptr<const GroupSet> groupPtr =
            groups.columns.empty() ? nullptr : std::make_shared<const GroupSet>(std::move(groups));
        switch (d) {
            case Domain::Points:
                out.pointAttrs = std::move(attrPtr);
                out.pointGroups = std::move(groupPtr);
                break;
            case Domain::Corners:
                out.cornerAttrs = std::move(attrPtr);
                out.cornerGroups = std::move(groupPtr);
                break;
            case Domain::Faces:
                out.faceAttrs = std::move(attrPtr);
                out.faceGroups = std::move(groupPtr);
                break;
            default:
                break;
        }
    }
    return std::make_shared<const Geo>(std::move(out));
}

}  // namespace pgg
