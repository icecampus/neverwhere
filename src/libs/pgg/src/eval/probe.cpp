#include "../../pch.h"

#include "probe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <unordered_set>

#include "sdf.h"

namespace pgg {
namespace {

std::string fmtG(float v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

std::string fmtG(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return buf;
}

std::string fmtVec3(const glm::vec3& v) {
    return "(" + fmtG(v.x) + ", " + fmtG(v.y) + ", " + fmtG(v.z) + ")";
}

const char* kDomainShort[] = {"pts", "corners", "faces", "detail"};
const Domain kDomainOrder[] = {Domain::Points, Domain::Corners, Domain::Faces, Domain::Detail};

const char* domainShort(Domain d) { return kDomainShort[static_cast<int>(d)]; }

const char* columnTypeName(const ColumnData& col) {
    switch (col.index()) {
        case 0: return "f32";
        case 1: return "int";
        case 2: return "bool";
        case 3: return "vec2";
        case 4: return "vec3";
        case 5: return "vec4";
        default: return "string";
    }
}

bool isStringColumn(const ColumnData& col) { return col.index() == 6; }

// Element value of a numeric column as f32 (bool -> 0/1, int -> float);
// component index for vector columns. Caller guarantees the type fits.
float columnValue(const ColumnData& col, size_t i, int comp) {
    switch (col.index()) {
        case 0: return (*std::get<std::shared_ptr<const std::vector<float>>>(col))[i];
        case 1: return static_cast<float>((*std::get<std::shared_ptr<const std::vector<int64_t>>>(col))[i]);
        case 2: return (*std::get<std::shared_ptr<const std::vector<uint8_t>>>(col))[i] ? 1.0f : 0.0f;
        case 3: return (*std::get<std::shared_ptr<const std::vector<glm::vec2>>>(col))[i][comp];
        case 4: return (*std::get<std::shared_ptr<const std::vector<glm::vec3>>>(col))[i][comp];
        case 5: return (*std::get<std::shared_ptr<const std::vector<glm::vec4>>>(col))[i][comp];
        default: return 0.0f;
    }
}

int columnWidth(const ColumnData& col) {
    switch (col.index()) {
        case 3: return 2;
        case 4: return 3;
        case 5: return 4;
        default: return 1;
    }
}

// Nearest-rank percentile: rank = ceil(p/100 * n), clamped to [1, n].
float nearestRank(std::vector<float>& sorted, double p) {
    const size_t n = sorted.size();
    if (n == 0) return 0.0f;
    const double r = std::ceil(p / 100.0 * static_cast<double>(n));
    const size_t rank = r < 1.0 ? 1 : (r > static_cast<double>(n) ? n : static_cast<size_t>(r));
    return sorted[rank - 1];
}

// One stats entry over one (component of a) numeric column.
ProbeStatsEntry statsOf(const std::string& label, const ColumnData& col, int comp, Domain domain) {
    ProbeStatsEntry e;
    e.label = label;
    e.domain = domainShort(domain);
    const size_t n = std::visit([](const auto& p) { return p ? p->size() : size_t(0); }, col);
    e.n = n;
    if (n == 0) return e;
    std::vector<float> vals(n);
    double sum = 0.0;
    e.mn = e.mx = columnValue(col, 0, comp);
    for (size_t i = 0; i < n; ++i) {  // @index order (storage order)
        const float v = columnValue(col, i, comp);
        vals[i] = v;
        sum += static_cast<double>(v);
        e.mn = std::min(e.mn, v);
        e.mx = std::max(e.mx, v);
    }
    e.mean = sum / static_cast<double>(n);
    std::sort(vals.begin(), vals.end());
    e.p50 = nearestRank(vals, 50.0);
    e.p90 = nearestRank(vals, 90.0);
    return e;
}

// Finds an attribute on the first domain that has it (pts, corners, faces,
// detail order). outDomain receives the domain found. The built-ins @P/@N
// resolve to their dedicated columns first (schema lists them; a named attr
// of the same name on another domain is shadowed).
const AttrColumn* findAttr(const Geo& g, const std::string& name, Domain& outDomain,
                           AttrColumn& builtIn) {
    if (name == "P" && g.positions) {
        builtIn.data = g.positions;
        outDomain = Domain::Points;
        return &builtIn;
    }
    if (name == "N" && g.normals) {
        builtIn.data = g.normals;
        outDomain = Domain::Points;
        return &builtIn;
    }
    for (Domain d : kDomainOrder) {
        const AttrSet* attrs = g.attrs(d);
        if (!attrs) continue;
        if (const AttrColumn* col = attrs->find(name)) {
            outDomain = d;
            return col;
        }
    }
    return nullptr;
}

// Finds a group on the first domain that has it.
ConstBoolColumnPtr findGroup(const Geo& g, const std::string& name, Domain& outDomain) {
    for (Domain d : kDomainOrder) {
        const GroupSet* groups = g.groups(d);
        if (!groups) continue;
        if (ConstBoolColumnPtr col = groups->find(name)) {
            outDomain = d;
            return col;
        }
    }
    return nullptr;
}

void appendColumnStats(const std::string& label, const ColumnData& col, Domain domain,
                       std::vector<ProbeStatsEntry>& out) {
    static const char* kComp[] = {".x", ".y", ".z", ".w"};
    const int w = columnWidth(col);
    for (int c = 0; c < w; ++c)
        out.push_back(statsOf(w > 1 ? label + kComp[c] : label, col, c, domain));
}

// Element of a column rendered for table rows (mirrors valueToString).
std::string columnElementText(const ColumnData& col, size_t i) {
    char buf[128];
    switch (col.index()) {
        case 0:
            std::snprintf(buf, sizeof(buf), "%g", (*std::get<std::shared_ptr<const std::vector<float>>>(col))[i]);
            return buf;
        case 1:
            return std::to_string((*std::get<std::shared_ptr<const std::vector<int64_t>>>(col))[i]);
        case 2:
            return (*std::get<std::shared_ptr<const std::vector<uint8_t>>>(col))[i] ? "true" : "false";
        case 3:
        case 4:
        case 5: {
            const int w = columnWidth(col);
            std::string out = "(";
            for (int c = 0; c < w; ++c) {
                std::snprintf(buf, sizeof(buf), "%g", columnValue(col, i, c));
                if (c) out += ", ";
                out += buf;
            }
            return out + ")";
        }
        default:
            return "\"" + (*std::get<std::shared_ptr<const std::vector<std::string>>>(col))[i] + "\"";
    }
}

// Sorted (name, domain) entries of every attribute incl. @N (the normals
// column); @P/@index are implicit and never listed.
struct SchemaAttr {
    std::string name;
    const char* type;
    Domain domain;
};

std::vector<SchemaAttr> schemaAttrs(const Geo& g) {
    std::vector<SchemaAttr> out;
    if (g.normals) out.push_back({"N", "vec3", Domain::Points});
    for (Domain d : kDomainOrder) {
        const AttrSet* attrs = g.attrs(d);
        if (!attrs) continue;
        for (const auto& [name, col] : attrs->columns)
            out.push_back({name, columnTypeName(col.data), d});
    }
    std::sort(out.begin(), out.end(), [](const SchemaAttr& a, const SchemaAttr& b) {
        if (a.name != b.name) return a.name < b.name;
        return static_cast<int>(a.domain) < static_cast<int>(b.domain);
    });
    return out;
}

std::vector<std::string> schemaGroups(const Geo& g) {
    std::vector<std::string> out;
    for (Domain d : kDomainOrder) {
        const GroupSet* groups = g.groups(d);
        if (!groups) continue;
        for (const auto& [name, col] : groups->columns) out.push_back(name);
    }
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

}  // namespace

// --- spec parsing -------------------------------------------------------------

bool parseProbeSpec(const std::string& text, ProbeSpec& out, std::string& err) {
    std::string head = text;
    // Trailing params: `[name=value, ...]` at the very end. The bracket only
    // counts as params when its content parses as a name=value list — an
    // instance index (`make_rock[1]`) is part of the path. Pairs split on
    // top-level commas only: vector values carry commas inside `(x,y,z)`.
    std::vector<std::pair<std::string, std::string>> pairs;
    if (!head.empty() && head.back() == ']') {
        const size_t open = head.rfind('[');
        if (open != std::string::npos) {
            const std::string content = head.substr(open + 1, head.size() - open - 2);
            bool looksLikeParams = true;
            int depth = 0;
            size_t segStart = 0;
            auto flush = [&](size_t end) {
                const std::string pair = content.substr(segStart, end - segStart);
                const size_t eq = pair.find('=');
                if (eq == std::string::npos || eq == 0) {
                    looksLikeParams = false;
                    return;
                }
                pairs.push_back({pair.substr(0, eq), pair.substr(eq + 1)});
            };
            for (size_t i = 0; i < content.size() && looksLikeParams; ++i) {
                const char c = content[i];
                if (c == '(') {
                    ++depth;
                } else if (c == ')') {
                    if (--depth < 0) {
                        looksLikeParams = false;
                        break;
                    }
                } else if (c == ',' && depth == 0) {
                    flush(i);
                    segStart = i + 1;
                }
            }
            // A trailing comma (`limit=2,`) leaves an empty tail — tolerated.
            if (looksLikeParams && depth == 0 && segStart < content.size()) flush(content.size());
            if (depth != 0) looksLikeParams = false;
            if (looksLikeParams && !pairs.empty()) head = head.substr(0, open);
            else pairs.clear();
        }
    }
    // `:inspector` — split on the first ':'.
    const size_t colon = head.find(':');
    if (colon != std::string::npos) {
        out.path = head.substr(0, colon);
        out.inspector = head.substr(colon + 1);
    } else {
        out.path = head;
    }
    if (out.path.empty()) {
        err = "empty probe path";
        return false;
    }
    if (out.path.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.[]") !=
        std::string::npos) {
        err = "malformed probe path '" + out.path + "'";
        return false;
    }
    if (!out.inspector.empty() && out.inspector != "schema" && out.inspector != "stats" &&
        out.inspector != "coverage" && out.inspector != "table" && out.inspector != "sample" &&
        out.inspector != "slice" && out.inspector != "check" && out.inspector != "lattice" &&
        out.inspector != "find") {
        err = "unknown inspector '" + out.inspector + "' (schema|stats|coverage|table|sample|slice|check|lattice|find)";
        return false;
    }
    // Param names: limit/aggregate are generic typed fields; every other name
    // must belong to the inspector's own list (§9.6).
    for (const auto& [name, value] : pairs) {
        if (name == "limit") {
            if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
                err = "bad limit value '" + value + "' (a positive integer expected)";
                return false;
            }
            out.limit = std::stoi(value);
            out.hasLimit = true;
            if (out.limit <= 0) {
                err = "bad limit value '" + value + "' (a positive integer expected)";
                return false;
            }
            continue;
        }
        if (name == "aggregate") {
            if (value != "stats") {
                err = "bad aggregate value '" + value + "' (only aggregate=stats is defined)";
                return false;
            }
            out.aggregate = true;
            continue;
        }
        // Inspector-specific names (§9.6); the message lists the valid set.
        std::string validForInspector = "limit|aggregate";
        bool known = false;
        if (out.inspector == "sample") {
            validForInspector = "at|from|to|n";
            known = name == "at" || name == "from" || name == "to" || name == "n";
        } else if (out.inspector == "slice") {
            validForInspector = "axis|at|step|bounds|format|iso";
            known = name == "axis" || name == "at" || name == "step" || name == "bounds" ||
                    name == "format" || name == "iso";
        } else if (out.inspector == "check") {
            validForInspector = "warn_aspect|limit";
            known = name == "warn_aspect";
        } else if (out.inspector == "lattice") {
            validForInspector = "voxel";
            known = name == "voxel";
        } else if (out.inspector == "table") {
            validForInspector = "where|limit";
            known = name == "where";
        } else if (out.inspector == "find") {
            validForInspector = "where";
            known = name == "where";
        }
        if (!known) {
            err = "unknown probe parameter '" + name + "' (" + validForInspector + ")";
            return false;
        }
        for (const auto& [seen, v] : out.params)
            if (seen == name) {
                err = "duplicate probe parameter '" + name + "'";
                return false;
            }
        out.params.push_back({name, value});
    }
    return true;
}

// --- L0: schema ---------------------------------------------------------------

std::string probeGeoSummary(const Geo& g) {
    switch (g.kind) {
        case GeoKind::Mesh: {
            size_t tri = 0;
            if (g.faceOffsets) {
                for (size_t f = 0; f + 1 < g.faceOffsets->size(); ++f) {
                    const int32_t corners = (*g.faceOffsets)[f + 1] - (*g.faceOffsets)[f];
                    if (corners >= 3) tri += static_cast<size_t>(corners - 2);
                }
            }
            return "mesh " + std::to_string(g.pointCount()) + " pts, " + std::to_string(tri) + " tri";
        }
        case GeoKind::Points:
            return "points " + std::to_string(g.pointCount()) + " pts";
        case GeoKind::Instances:
            return "instances " + std::to_string(g.pointCount()) + " anchors, " +
                   std::to_string(g.instanceSources ? g.instanceSources->size() : 0) + " variants";
        default:
            return "geo";
    }
}

std::string probeGeoSchema(const Geo& g) {
    std::string out = probeGeoSummary(g);
    const std::vector<SchemaAttr> attrs = schemaAttrs(g);
    if (!attrs.empty()) {
        out += "; attrs: ";
        bool first = true;
        for (const SchemaAttr& a : attrs) {
            if (!first) out += ", ";
            first = false;
            out += a.name + "(" + a.type + ", " + domainShort(a.domain) + ")";
        }
    }
    const std::vector<std::string> groups = schemaGroups(g);
    if (!groups.empty()) {
        out += "; groups: ";
        for (size_t i = 0; i < groups.size(); ++i) out += (i ? ", " : "") + groups[i];
    }
    return out;
}

std::string probeSchema(const Value& v) {
    switch (v.data.index()) {
        case 8: return "<rng>";
        case 9: return probeGeoSchema(*asGeo(v));
        case 12: {
            const SdfNode& sdf = *asSdf(v);
            glm::vec3 mn, mx;
            sdf.conservativeBBox(mn, mx);
            std::string out = "sdf nodes=" + std::to_string(sdfNodeCount(sdf));
            if (mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z) {
                out += " bbox=(" + fmtG(mn.x) + ", " + fmtG(mn.y) + ", " + fmtG(mn.z) + ")..(" +
                       fmtG(mx.x) + ", " + fmtG(mx.y) + ", " + fmtG(mx.z) + ")";
            } else {
                out += " bbox=(empty)";
            }
            return out;
        }
        case 11: {
            const auto& elems = asList(v);
            std::string out = "list[" + std::to_string(elems.size()) + "]";
            if (!elems.empty()) {
                const Value& e = elems.front();
                out += " of ";
                if (valueBase(e) == ScalarType::Geo) {
                    out += std::string("geo<") + geoKindName(asGeo(e)->kind) + ">";
                } else if (isListValue(e)) {
                    out += "list";
                } else {
                    out += scalarName(valueBase(e));
                }
            }
            return out;
        }
        default:
            return std::string(scalarName(valueBase(v))) + " " + valueToString(v);
    }
}

// --- L1: stats ----------------------------------------------------------------

bool probeGeoStats(const Geo& g, const std::string& terminal,
                   std::vector<ProbeStatsEntry>& out, std::string& err) {
    if (!terminal.empty()) {
        Domain domain = Domain::Points;
        AttrColumn builtIn;
        if (const AttrColumn* col = findAttr(g, terminal, domain, builtIn)) {
            if (isStringColumn(col->data)) {
                err = "stats target '" + terminal + "' is not numeric (string)";
                return false;
            }
            appendColumnStats(terminal, col->data, domain, out);
            return true;
        }
        if (ConstBoolColumnPtr grp = findGroup(g, terminal, domain)) {
            ColumnData col = grp;
            appendColumnStats(terminal, col, domain, out);
            return true;
        }
        err = "probe target '" + terminal + "' not found on the geometry (no such attribute or group)";
        return false;
    }
    // No terminal: every numeric point attribute, sorted by name (@N listed).
    std::map<std::string, const ColumnData*> numeric;
    if (const AttrSet* attrs = g.attrs(Domain::Points))
        for (const auto& [name, col] : attrs->columns)
            if (!isStringColumn(col.data)) numeric[name] = &col.data;
    for (const auto& [name, col] : numeric) appendColumnStats(name, *col, Domain::Points, out);
    if (g.normals) {
        ColumnData col = g.normals;
        appendColumnStats("N", col, Domain::Points, out);
    }
    // Sorted by label: map order for named attrs, then merge N into place.
    std::sort(out.begin(), out.end(), [](const ProbeStatsEntry& a, const ProbeStatsEntry& b) {
        return a.label < b.label;
    });
    return true;
}

bool probeValueStats(const Value& v, std::vector<ProbeStatsEntry>& out, std::string& err) {
    const ScalarType base = valueBase(v);
    if (base == ScalarType::Bool || base == ScalarType::Int || base == ScalarType::F32) {
        ColumnData col = std::make_shared<const std::vector<float>>(1, numericValueF32(v));
        out.push_back(statsOf("value", col, 0, Domain::Detail));
        out.back().domain = "value";
        return true;
    }
    if (isVectorBase(base)) {
        static const char* kComp[] = {".x", ".y", ".z", ".w"};
        const int w = vecWidth(base);
        for (int c = 0; c < w; ++c) {
            const float f = base == ScalarType::Vec2 ? asVec2(v)[c]
                            : base == ScalarType::Vec3 ? asVec3(v)[c]
                                                       : asVec4(v)[c];
            ColumnData col = std::make_shared<const std::vector<float>>(1, f);
            out.push_back(statsOf(std::string("value") + kComp[c], col, 0, Domain::Detail));
            out.back().domain = "value";
        }
        return true;
    }
    err = std::string("stats target is not numeric (") + scalarName(base) + ")";
    return false;
}

std::string formatProbeStats(const ProbeStatsEntry& e) {
    return e.label + ": mean " + fmtG(e.mean) + ", p50 " + fmtG(e.p50) + ", p90 " + fmtG(e.p90) +
           ", min " + fmtG(e.mn) + ", max " + fmtG(e.mx) + " (" + std::to_string(e.n) + " " + e.domain +
           ")";
}

// --- L1: coverage -------------------------------------------------------------

bool probeGeoCoverage(const Geo& g, const std::string& terminal,
                      ProbeCoverage& out, std::string& err) {
    if (terminal.empty()) {
        err = "coverage needs a bool attribute or group terminal (path.attr)";
        return false;
    }
    Domain domain = Domain::Points;
    const uint8_t* data = nullptr;
    size_t n = 0;
    AttrColumn builtIn;
    if (const AttrColumn* col = findAttr(g, terminal, domain, builtIn)) {
        if (col->data.index() != 2) {
            err = "coverage target '" + terminal + "' is not a bool mask";
            return false;
        }
        const auto& buf = *std::get<std::shared_ptr<const std::vector<uint8_t>>>(col->data);
        data = buf.data();
        n = buf.size();
    } else if (ConstBoolColumnPtr grp = findGroup(g, terminal, domain)) {
        data = grp->data();
        n = grp->size();
    } else {
        err = "probe target '" + terminal + "' not found on the geometry (no such attribute or group)";
        return false;
    }
    out.label = terminal;
    out.n = n;
    for (size_t i = 0; i < n; ++i) out.t += data[i] ? 1 : 0;
    return true;
}

std::string formatProbeCoverage(const ProbeCoverage& c) {
    char buf[32];
    const double pct = c.n > 0 ? 100.0 * static_cast<double>(c.t) / static_cast<double>(c.n) : 0.0;
    std::snprintf(buf, sizeof(buf), "%.1f", pct);
    return c.label + ": true " + buf + "% (" + std::to_string(c.t) + "/" + std::to_string(c.n) + ")";
}

// --- L2: table ----------------------------------------------------------------

std::string probeGeoTable(const Geo& g, int limit, const BoolColumn* mask, const std::string& whereEcho) {
    const size_t n = g.pointCount();
    std::vector<size_t> sel;  // selected point indices (@index order)
    if (mask) {
        for (size_t i = 0; i < n && i < mask->size(); ++i)
            if ((*mask)[i]) sel.push_back(i);
    }
    const size_t rows = mask ? sel.size() : n;
    const size_t k = std::min(rows, static_cast<size_t>(std::max(limit, 0)));
    std::string out;
    if (mask) {
        out = "table[where=" + whereEcho + ",limit=" + std::to_string(limit) + "] (first " +
              std::to_string(k) + " of " + std::to_string(rows) + " matching, " + std::to_string(n) + " total)";
    } else {
        out = "table[limit=" + std::to_string(limit) + "] (first " + std::to_string(k) + " of " +
              std::to_string(n) + " by @index)";
    }
    // Cols: @P + point attributes sorted by name (@N listed among them).
    ColumnData positions = g.positions;
    std::map<std::string, ColumnData> cols;
    if (const AttrSet* attrs = g.attrs(Domain::Points))
        for (const auto& [name, col] : attrs->columns) cols[name] = col.data;
    if (g.normals) cols["N"] = g.normals;
    for (size_t r = 0; r < k; ++r) {
        const size_t i = mask ? sel[r] : r;
        out += "\n" + std::to_string(i) + ": @P=" + columnElementText(positions, i);
        for (const auto& [name, col] : cols) out += ", " + name + "=" + columnElementText(col, i);
    }
    return out;
}

// --- L2: find -------------------------------------------------------------------

std::string probeGeoFind(const Geo& g, const BoolColumn& mask, const std::string& whereEcho) {
    const size_t n = g.pointCount();
    size_t k = 0;
    glm::vec3 mn(0.0f), mx(0.0f);
    const auto& pos = g.positions;
    for (size_t i = 0; i < n && i < mask.size(); ++i) {
        if (!mask[i]) continue;
        const glm::vec3 p = pos ? (*pos)[i] : glm::vec3(0.0f);
        if (k == 0) {
            mn = mx = p;
        } else {
            mn = glm::min(mn, p);
            mx = glm::max(mx, p);
        }
        ++k;
    }
    std::string out = "find[where=" + whereEcho + "]\ncount " + std::to_string(k) + " of " +
                      std::to_string(n);
    if (k > 0) out += "\nbbox " + fmtVec3(mn) + ".." + fmtVec3(mx);
    // Points-domain groups with selected members (sorted by name).
    if (const GroupSet* groups = g.groups(Domain::Points)) {
        std::map<std::string, size_t> hit;
        for (const auto& [name, col] : groups->columns) {
            size_t c = 0;
            for (size_t i = 0; i < n && i < mask.size() && i < col->size(); ++i)
                if (mask[i] && (*col)[i]) ++c;
            if (c > 0) hit[name] = c;
        }
        if (!hit.empty()) {
            out += "\ngroups: ";
            bool first = true;
            for (const auto& [name, c] : hit) {
                if (!first) out += ", ";
                first = false;
                out += name + " (" + std::to_string(c) + ")";
            }
        }
    }
    return out;
}

// --- L2: sample / slice shared field access (§9.6) ------------------------------

namespace {

const std::string* findProbeParam(const std::vector<std::pair<std::string, std::string>>& params,
                                  const std::string& name) {
    for (const auto& [n, v] : params)
        if (n == name) return &v;
    return nullptr;
}

std::string trimSpaces(const std::string& s) {
    const size_t b = s.find_first_not_of(' ');
    if (b == std::string::npos) return "";
    return s.substr(b, s.find_last_not_of(' ') - b + 1);
}

// Full-string float (' 3.425 ' tolerated); no partial parses.
bool parseFloatRaw(const std::string& s, float& out) {
    const std::string t = trimSpaces(s);
    if (t.empty()) return false;
    char* end = nullptr;
    const float v = std::strtof(t.c_str(), &end);
    if (end != t.c_str() + t.size()) return false;
    out = v;
    return true;
}

bool parseIntRaw(const std::string& s, int& out) {
    const std::string t = trimSpaces(s);
    if (t.empty()) return false;
    char* end = nullptr;
    const long v = std::strtol(t.c_str(), &end, 10);
    if (end != t.c_str() + t.size()) return false;
    out = static_cast<int>(v);
    return true;
}

// `(f, f, ...)` — exactly `width` floats inside parens.
bool parseVecRaw(const std::string& s, int width, float* out) {
    const std::string t = trimSpaces(s);
    if (t.size() < 2 || t.front() != '(' || t.back() != ')') return false;
    const std::string inner = t.substr(1, t.size() - 2);
    size_t start = 0;
    for (int c = 0; c < width; ++c) {
        const size_t comma = inner.find(',', start);
        if (c + 1 < width && comma == std::string::npos) return false;
        if (c + 1 == width && comma != std::string::npos) return false;
        if (!parseFloatRaw(inner.substr(start, comma == std::string::npos ? comma : comma - start), out[c]))
            return false;
        start = comma == std::string::npos ? inner.size() : comma + 1;
    }
    return true;
}

bool parseVec3Raw(const std::string& s, glm::vec3& out) {
    float f[3];
    if (!parseVecRaw(s, 3, f)) return false;
    out = glm::vec3(f[0], f[1], f[2]);
    return true;
}

// `(x,y,z);(x2,y2,z2);...` — `;`-separated vec3 list in one value.
bool parseVec3ListRaw(const std::string& s, std::vector<glm::vec3>& out) {
    size_t start = 0;
    while (true) {
        const size_t semi = s.find(';', start);
        glm::vec3 v;
        if (!parseVec3Raw(s.substr(start, semi == std::string::npos ? semi : semi - start), v)) return false;
        out.push_back(v);
        if (semi == std::string::npos) break;
        start = semi + 1;
    }
    return !out.empty();
}

// At-point field shared by sample/slice: the signed sdf field, the mesh
// pseudo-sign distance (BVH closest point, sign by the closest triangle's
// normal — the same oracle sdf_from_mesh uses) or the unsigned nearest-point
// distance of a points geometry. `note` describes the non-sdf modes for the
// record header; bboxMn/bboxMx carry the (conservative) bbox for slice's
// default bounds.
struct ProbeField {
    std::function<float(const glm::vec3&)> eval;
    std::string note;
    glm::vec3 bboxMn{0.0f};
    glm::vec3 bboxMx{0.0f};
    MeshBvh bvh;  // mesh mode only; eval captures this member
};

bool probeFieldFor(const Value& v, ProbeField& out, std::string& err) {
    if (valueBase(v) == ScalarType::Sdf) {
        const SdfNode& sdf = *asSdf(v);
        out.eval = [&sdf](const glm::vec3& p) { return sdf.eval(p); };
        sdf.conservativeBBox(out.bboxMn, out.bboxMx);
        return true;
    }
    if (valueBase(v) != ScalarType::Geo) {
        err = std::string("target is ") + scalarName(valueBase(v)) + " (an sdf or geo value expected)";
        return false;
    }
    const Geo& g = *asGeo(v);
    geoBBox(g, out.bboxMn, out.bboxMx);
    if (g.kind == GeoKind::Mesh) {
        if (g.faceCount() == 0) {
            err = "target mesh has no faces to measure distance to";
            return false;
        }
        out.note = "pseudo-sign distance from mesh";
        out.bvh.build(g);
        ProbeField* self = &out;
        out.eval = [self](const glm::vec3& p) {
            float dist = 0.0f;
            glm::vec3 cp, n;
            if (!self->bvh.closest(p, dist, cp, n)) return 0.0f;
            return glm::dot(p - cp, n) >= 0.0f ? dist : -dist;
        };
        return true;
    }
    if (g.kind == GeoKind::Points) {
        if (g.pointCount() == 0) {
            err = "target geometry has no points to measure distance to";
            return false;
        }
        out.note = "unsigned distance from points";
        const std::shared_ptr<const std::vector<glm::vec3>> pos = g.positions;
        out.eval = [pos](const glm::vec3& p) {
            float best = std::numeric_limits<float>::max();
            for (const glm::vec3& q : *pos) best = std::min(best, glm::distance(p, q));
            return best;
        };
        return true;
    }
    err = "target is geo<instances> (an sdf, geo<mesh> or geo<points> value expected)";
    return false;
}

}  // namespace

// --- L2: sample -----------------------------------------------------------------

bool probeSample(const Value& v, const std::vector<std::pair<std::string, std::string>>& params,
                 std::string& out, std::string& err) {
    std::vector<glm::vec3> points;
    std::string echo;
    const std::string* at = findProbeParam(params, "at");
    if (at) {
        if (findProbeParam(params, "from") || findProbeParam(params, "to") || findProbeParam(params, "n")) {
            err = "sample mixes forms: at=(x,y,z)[;...] stands alone, from/to/n form the profile";
            return false;
        }
        if (!parseVec3ListRaw(*at, points)) {
            err = "bad at value '" + *at + "' (vec3 points in parens expected: (x,y,z)[;(x,y,z);...])";
            return false;
        }
        echo = "at=";
        for (size_t i = 0; i < points.size(); ++i) echo += (i ? ";" : "") + fmtVec3(points[i]);
    } else {
        const std::string* from = findProbeParam(params, "from");
        const std::string* to = findProbeParam(params, "to");
        if (!from && !to) {
            err = "sample needs at=(x,y,z)[;(x,y,z);...] or from=(x,y,z),to=(x,y,z)[,n=41]";
            return false;
        }
        if (!from || !to) {
            err = "sample profile needs both from=(x,y,z) and to=(x,y,z)";
            return false;
        }
        glm::vec3 a, b;
        if (!parseVec3Raw(*from, a)) {
            err = "bad from value '" + *from + "' (a parenthesised vec3 expected: (x,y,z))";
            return false;
        }
        if (!parseVec3Raw(*to, b)) {
            err = "bad to value '" + *to + "' (a parenthesised vec3 expected: (x,y,z))";
            return false;
        }
        int n = 41;
        if (const std::string* ns = findProbeParam(params, "n")) {
            if (!parseIntRaw(*ns, n) || n < 2) {
                err = "bad n value '" + *ns + "' (an integer >= 2 expected)";
                return false;
            }
        }
        points.reserve(static_cast<size_t>(n));
        for (int i = 0; i < n; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(n - 1);
            points.push_back(a + (b - a) * t);
        }
        echo = "from=" + fmtVec3(a) + ",to=" + fmtVec3(b) + ",n=" + std::to_string(n);
    }
    ProbeField field;
    if (!probeFieldFor(v, field, err)) return false;
    out = "sample[" + echo + "]" + (field.note.empty() ? "" : " (" + field.note + ")");
    for (const glm::vec3& p : points)
        out += "\n" + fmtG(p.x) + " " + fmtG(p.y) + " " + fmtG(p.z) + " " + fmtG(field.eval(p));
    return true;
}

// --- L2: slice ------------------------------------------------------------------

bool probeSlice(const Value& v, const std::vector<std::pair<std::string, std::string>>& params,
                std::string& out, std::string& err) {
    const std::string* axisP = findProbeParam(params, "axis");
    if (!axisP) {
        err = "slice needs axis=x|y|z";
        return false;
    }
    if (*axisP != "x" && *axisP != "y" && *axisP != "z") {
        err = "bad axis value '" + *axisP + "' (x|y|z expected)";
        return false;
    }
    const int axisIdx = (*axisP)[0] - 'x';
    float atV = 0.0f, step = 0.0f, iso = 0.0f;
    const std::string* atP = findProbeParam(params, "at");
    if (!atP) {
        err = "slice needs at=<plane coordinate>";
        return false;
    }
    if (!parseFloatRaw(*atP, atV)) {
        err = "bad at value '" + *atP + "' (a number expected)";
        return false;
    }
    const std::string* stepP = findProbeParam(params, "step");
    if (!stepP) {
        err = "slice needs step=<grid cell size>";
        return false;
    }
    if (!parseFloatRaw(*stepP, step) || !(step > 0.0f)) {
        err = "bad step value '" + *stepP + "' (a positive number expected)";
        return false;
    }
    if (const std::string* isoP = findProbeParam(params, "iso")) {
        if (!parseFloatRaw(*isoP, iso)) {
            err = "bad iso value '" + *isoP + "' (a number expected)";
            return false;
        }
    }
    std::string format = "ascii";
    if (const std::string* fmtP = findProbeParam(params, "format")) {
        format = *fmtP;
        if (format != "ascii" && format != "csv") {
            err = "bad format value '" + *fmtP + "' (ascii|csv expected)";
            return false;
        }
    }
    ProbeField field;
    if (!probeFieldFor(v, field, err)) return false;
    // Plane coords: axis=x -> (y,z), axis=y -> (x,z), axis=z -> (x,y).
    const int u = (axisIdx + 1) % 3, w = (axisIdx + 2) % 3;
    float u0, v0, u1, v1;
    if (const std::string* boundsP = findProbeParam(params, "bounds")) {
        float b[4];
        if (!parseVecRaw(*boundsP, 4, b)) {
            err = "bad bounds value '" + *boundsP + "' ((u0,v0,u1,v1) in parens expected)";
            return false;
        }
        u0 = b[0];
        v0 = b[1];
        u1 = b[2];
        v1 = b[3];
        if (!(u1 >= u0) || !(v1 >= v0)) {
            err = "bad bounds value '" + *boundsP + "' (u1 >= u0 and v1 >= v0 expected)";
            return false;
        }
    } else {
        u0 = field.bboxMn[u];
        u1 = field.bboxMx[u];
        v0 = field.bboxMn[w];
        v1 = field.bboxMx[w];
        if (!(u0 <= u1) || !(v0 <= v1)) {
            err = "slice bounds are empty (the target has no finite bbox; pass bounds=(u0,v0,u1,v1))";
            return false;
        }
    }
    int width = static_cast<int>(std::lround((u1 - u0) / step)) + 1;
    int height = static_cast<int>(std::lround((v1 - v0) / step)) + 1;
    float effStep = step;
    std::string adjustNote;
    if (width > 120) {
        effStep = (u1 - u0) / 119.0f;
        width = 120;
        height = static_cast<int>(std::lround((v1 - v0) / effStep)) + 1;
        adjustNote = ", step widened to " + fmtG(effStep) + " (120 col cap)";
    }
    // Header: the given params normalised, in written order.
    std::string echo;
    for (const auto& [name, value] : params) {
        if (!echo.empty()) echo += ",";
        if (name == "axis")
            echo += "axis=" + value;
        else if (name == "at")
            echo += "at=" + fmtG(atV);
        else if (name == "step")
            echo += "step=" + fmtG(step);
        else if (name == "bounds")
            echo += "bounds=(" + fmtG(u0) + ", " + fmtG(v0) + ", " + fmtG(u1) + ", " + fmtG(v1) + ")";
        else if (name == "format")
            echo += "format=" + format;
        else if (name == "iso")
            echo += "iso=" + fmtG(iso);
    }
    out = "slice[" + echo + "] (" + std::to_string(width) + " x " + std::to_string(height) + ", bounds (" +
          fmtG(u0) + ", " + fmtG(v0) + ")..(" + fmtG(u1) + ", " + fmtG(v1) + ")" + adjustNote +
          (field.note.empty() ? "" : ", " + field.note) + ")";
    const auto valueAt = [&](int i, int j) {
        glm::vec3 p(0.0f);
        p[axisIdx] = atV;
        p[u] = u0 + i * effStep;
        p[w] = v0 + j * effStep;
        return field.eval(p);
    };
    if (format == "csv") {
        for (int j = 0; j < height; ++j)
            for (int i = 0; i < width; ++i)
                out += "\n" + std::to_string(i) + "," + std::to_string(j) + "," + fmtG(valueAt(i, j));
    } else {
        for (int j = height - 1; j >= 0; --j) {  // first row = v_max (map view)
            out += "\n";
            for (int i = 0; i < width; ++i) out += valueAt(i, j) <= iso ? '#' : '.';
        }
    }
    return true;
}

// --- L2: check ------------------------------------------------------------------

namespace {

// Face aspect: max over the fan triangles of longest_edge^2 / (2 * area) —
// the longest edge over the triangle's smallest altitude. Zero-area fan
// triangles are skipped (they are counted as degenerate, not needles).
float faceAspect(const std::vector<glm::vec3>& P, const int32_t* corners, int32_t count) {
    float worst = 0.0f;
    const glm::vec3& a = P[corners[0]];
    for (int32_t k = 1; k + 1 < count; ++k) {
        const glm::vec3& b = P[corners[k]];
        const glm::vec3& c = P[corners[k + 1]];
        const float ab = glm::distance(a, b), bc = glm::distance(b, c), ca = glm::distance(c, a);
        const float longest = std::max(ab, std::max(bc, ca));
        const float area2 = glm::length(glm::cross(b - a, c - a));  // 2 * area
        if (area2 == 0.0f) continue;
        worst = std::max(worst, longest * longest / area2);
    }
    return worst;
}

}  // namespace

bool classifyMeshIssueFaces(const Geo& g, MeshIssueFaces& out) {
    out = MeshIssueFaces{};
    if (g.kind != GeoKind::Mesh) return false;
    static const std::vector<glm::vec3> kNoPos;
    static const std::vector<int32_t> kNoIdx;
    const std::vector<glm::vec3>& P = g.positions ? *g.positions : kNoPos;
    const std::vector<int32_t>& cv = g.cornerVerts ? *g.cornerVerts : kNoIdx;
    const std::vector<int32_t>& fo = g.faceOffsets ? *g.faceOffsets : kNoIdx;
    const size_t np = P.size();
    const size_t nf = fo.size() > 0 ? fo.size() - 1 : 0;

    // Pass 1: degenerate faces (zero Newell area / repeated / out-of-range
    // corner indices / < 3 corners) and the undirected edge incidence of the
    // intact faces. Broken faces add no edge evidence (their winding is
    // meaningless).
    std::map<std::pair<int32_t, int32_t>, int32_t> edgeUse;  // (lo,hi) -> side count
    std::vector<uint8_t> isDegen(nf, 0);
    for (size_t f = 0; f < nf; ++f) {
        const int32_t begin = fo[f], end = fo[f + 1];
        bool degen = end - begin < 3;
        for (int32_t k = begin; k < end && !degen; ++k) {
            if (cv[k] < 0 || static_cast<size_t>(cv[k]) >= np) degen = true;
            for (int32_t m = begin; m < k && !degen; ++m)
                if (cv[m] == cv[k]) degen = true;
        }
        if (!degen) {
            glm::vec3 n(0.0f);  // Newell
            for (int32_t k = begin; k < end; ++k) {
                const glm::vec3& a = P[cv[k]];
                const glm::vec3& b = P[cv[k + 1 < end ? k + 1 : begin]];
                n.x += (a.y - b.y) * (a.z + b.z);
                n.y += (a.z - b.z) * (a.x + b.x);
                n.z += (a.x - b.x) * (a.y + b.y);
            }
            if (glm::length(n) == 0.0f) degen = true;
        }
        if (degen) {
            isDegen[f] = 1;
            out.degenerate.push_back(static_cast<int32_t>(f));
            continue;
        }
        for (int32_t k = begin; k < end; ++k) {
            const int32_t a = cv[k], b = cv[k + 1 < end ? k + 1 : begin];
            if (a == b) continue;
            edgeUse[{std::min(a, b), std::max(a, b)}] += 1;
        }
    }
    for (const auto& [e, count] : edgeUse) {
        if (count == 1) ++out.boundaryEdges;
        else if (count > 2) ++out.nonmanifoldEdges;
    }
    // Pass 2: the face sets — intact faces incident to a classified edge.
    if (!edgeUse.empty()) {
        std::vector<uint8_t> nm(nf, 0), bd(nf, 0);
        for (size_t f = 0; f < nf; ++f) {
            if (isDegen[f]) continue;
            const int32_t begin = fo[f], end = fo[f + 1];
            for (int32_t k = begin; k < end; ++k) {
                const int32_t a = cv[k], b = cv[k + 1 < end ? k + 1 : begin];
                if (a == b) continue;
                const auto it = edgeUse.find({std::min(a, b), std::max(a, b)});
                if (it == edgeUse.end()) continue;
                if (it->second == 1) bd[f] = 1;
                else if (it->second > 2) nm[f] = 1;
            }
        }
        for (size_t f = 0; f < nf; ++f) {
            if (nm[f]) out.nonmanifold.push_back(static_cast<int32_t>(f));
            if (bd[f]) out.boundary.push_back(static_cast<int32_t>(f));
        }
    }
    return true;
}

bool probeGeoCheck(const Geo& g, const std::vector<std::pair<std::string, std::string>>& params,
                   int limit, std::string& out, std::string& err) {
    if (g.kind != GeoKind::Mesh) {
        err = std::string("check needs a geo<mesh> value (target is geo<") + geoKindName(g.kind) + ">)";
        return false;
    }
    float warnAspect = 20.0f;
    if (const std::string* wa = findProbeParam(params, "warn_aspect")) {
        if (!parseFloatRaw(*wa, warnAspect) || !(warnAspect > 0.0f)) {
            err = "bad warn_aspect value '" + *wa + "' (a positive number expected)";
            return false;
        }
    }
    static const std::vector<glm::vec3> kNoPos;
    static const std::vector<int32_t> kNoIdx;
    const std::vector<glm::vec3>& P = g.positions ? *g.positions : kNoPos;
    const std::vector<int32_t>& cv = g.cornerVerts ? *g.cornerVerts : kNoIdx;
    const std::vector<int32_t>& fo = g.faceOffsets ? *g.faceOffsets : kNoIdx;
    const size_t np = P.size();
    const size_t nf = fo.size() > 0 ? fo.size() - 1 : 0;

    // Components: union-find over faces via shared points (degenerate faces
    // connect too — a broken face still references its points).
    std::vector<int32_t> parent(nf);
    for (size_t f = 0; f < nf; ++f) parent[f] = static_cast<int32_t>(f);
    const std::function<int32_t(int32_t)> find = [&](int32_t x) {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x = parent[x];
        }
        return x;
    };
    // Per-face pass: used points, directed edges, edge lengths. The
    // degenerate/nonmanifold/boundary classification itself lives in
    // classifyMeshIssueFaces (shared with the OBJ check-coloring); broken
    // faces add no edge/orientation evidence (their winding is meaningless),
    // but their points are NOT isolated.
    MeshIssueFaces issueFaces;
    classifyMeshIssueFaces(g, issueFaces);
    const std::vector<int32_t>& degenFaces = issueFaces.degenerate;
    const size_t boundary = issueFaces.boundaryEdges;
    const size_t nonmanifold = issueFaces.nonmanifoldEdges;
    std::vector<uint8_t> isDegen(nf, 0);
    for (const int32_t f : degenFaces) isDegen[static_cast<size_t>(f)] = 1;
    std::vector<std::pair<int32_t, int32_t>> dirEdges;  // directed, a != b
    std::vector<int32_t> dirEdgeFace;
    std::vector<float> edgeLens;
    std::vector<uint8_t> used(np, 0);
    std::vector<int32_t> firstFace(np, -1);
    for (size_t f = 0; f < nf; ++f) {
        const int32_t begin = fo[f], end = fo[f + 1];
        for (int32_t k = begin; k < end; ++k) {
            const int32_t a = cv[k];
            if (a < 0 || static_cast<size_t>(a) >= np) continue;
            used[a] = 1;
            if (firstFace[a] == -1) {
                firstFace[a] = static_cast<int32_t>(f);
            } else if (firstFace[a] != static_cast<int32_t>(f)) {
                const int32_t ra = find(firstFace[a]), rb = find(static_cast<int32_t>(f));
                if (ra != rb) parent[std::max(ra, rb)] = std::min(ra, rb);
            }
        }
        if (isDegen[f]) continue;
        for (int32_t k = begin; k < end; ++k) {
            const int32_t a = cv[k], b = cv[k + 1 < end ? k + 1 : begin];
            if (a == b) continue;
            dirEdges.push_back({a, b});
            dirEdgeFace.push_back(static_cast<int32_t>(f));
            edgeLens.push_back(glm::distance(P[a], P[b]));
        }
    }
    size_t isolated = 0;
    for (const uint8_t u : used)
        if (!u) ++isolated;
    // Points with a non-finite component in @P/@N/@Cd.
    size_t nan = 0;
    {
        const glm::vec3* cd = nullptr;
        if (const AttrSet* attrs = g.attrs(Domain::Points)) {
            if (const AttrColumn* col = attrs->find("Cd")) {
                if (col->data.index() == 4 &&
                    (*std::get<std::shared_ptr<const std::vector<glm::vec3>>>(col->data)).size() >= np)
                    cd = (*std::get<std::shared_ptr<const std::vector<glm::vec3>>>(col->data)).data();
            }
        }
        const glm::vec3* nrm = g.normals && g.normals->size() >= np ? g.normals->data() : nullptr;
        for (size_t i = 0; i < np; ++i) {
            const glm::vec3 p = P[i];
            bool bad = !std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z);
            if (!bad && nrm) {
                const glm::vec3 q = nrm[i];
                bad = !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z);
            }
            if (!bad && cd) {
                const glm::vec3 q = cd[i];
                bad = !std::isfinite(q.x) || !std::isfinite(q.y) || !std::isfinite(q.z);
            }
            if (bad) ++nan;
        }
    }
    // Components: union-find over faces via shared points ran in the face
    // pass above (degenerate faces connect too).
    size_t components = 0;
    for (size_t f = 0; f < nf; ++f)
        if (find(static_cast<int32_t>(f)) == static_cast<int32_t>(f)) ++components;
    // Orientation: a directed edge (a,b) seen 2+ times inside one component
    // means two adjacent faces wind the same way — the component is
    // mismatched (a consistently oriented closed mesh walks every edge once
    // per direction).
    size_t orientedMismatch = 0;
    {
        std::map<std::pair<int32_t, int64_t>, int32_t> dirCount;  // (root, (a,b)) -> count
        std::vector<uint8_t> flagged(nf, 0);
        for (size_t k = 0; k < dirEdges.size(); ++k) {
            const int32_t root = find(dirEdgeFace[k]);
            const int64_t key = (static_cast<int64_t>(dirEdges[k].first) << 32) |
                                static_cast<uint32_t>(dirEdges[k].second);
            if (++dirCount[{root, key}] == 2) flagged[root] = 1;
        }
        for (size_t f = 0; f < nf; ++f)
            if (flagged[f]) ++orientedMismatch;
    }
    // Needles.
    std::vector<int32_t> needleFaces;
    for (size_t f = 0; f < nf; ++f) {
        const int32_t begin = fo[f], end = fo[f + 1];
        bool valid = end - begin >= 3;
        for (int32_t k = begin; k < end && valid; ++k)
            if (cv[k] < 0 || static_cast<size_t>(cv[k]) >= np) valid = false;
        if (!valid) continue;
        if (faceAspect(P, cv.data() + begin, end - begin) > warnAspect)
            needleFaces.push_back(static_cast<int32_t>(f));
    }

    const size_t issues = degenFaces.size() + nonmanifold + isolated + nan + orientedMismatch + needleFaces.size();
    const auto indexList = [&](const std::vector<int32_t>& faces) {
        std::string s;
        const size_t k = std::min(faces.size(), static_cast<size_t>(std::max(limit, 0)));
        for (size_t i = 0; i < k; ++i) s += (i ? " " : "") + std::to_string(faces[i]);
        return s;
    };
    out = "degenerate " + std::to_string(degenFaces.size());
    if (!degenFaces.empty()) out += "\ndegenerate_faces " + indexList(degenFaces);
    out += "\nnonmanifold " + std::to_string(nonmanifold);
    out += "\nboundary " + std::to_string(boundary);
    out += "\nisolated " + std::to_string(isolated);
    out += "\nnan " + std::to_string(nan);
    out += "\ncomponents " + std::to_string(components);
    out += "\noriented_mismatch " + std::to_string(orientedMismatch);
    if (edgeLens.empty()) {
        out += "\nedge_min -";
        out += "\nedge_median -";
    } else {
        std::sort(edgeLens.begin(), edgeLens.end());
        out += "\nedge_min " + fmtG(edgeLens.front());
        out += "\nedge_median " + fmtG(nearestRank(edgeLens, 50.0));
    }
    out += "\nneedles " + std::to_string(needleFaces.size());
    if (!needleFaces.empty()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f",
                      100.0 * static_cast<double>(needleFaces.size()) / static_cast<double>(std::max(nf, size_t(1))));
        out += "\nneedle_ratio " + std::string(buf) + "%";
        out += "\nneedles_faces " + indexList(needleFaces);
    }
    out += issues == 0 ? "\nok" : "\nissues " + std::to_string(issues);
    return true;
}

// --- L2: lattice (§9.6) ----------------------------------------------------------

namespace {

// Axis-aligned transform: translation + uniform scale + a signed axis
// permutation (the class of rotations under which Box face planes stay axis
// planes). img[i] = world axis of local axis i; sgn[i] = its sign.
struct AxisXform {
    glm::vec3 offset{0.0f};
    float scale = 1.0f;
    int img[3] = {0, 1, 2};
    int sgn[3] = {1, 1, 1};

    bool isIdentity() const {
        return offset == glm::vec3(0.0f) && scale == 1.0f && img[0] == 0 && img[1] == 1 &&
               img[2] == 2 && sgn[0] == 1 && sgn[1] == 1 && sgn[2] == 1;
    }
    // world[img[i]] = offset[img[i]] + sgn[i]*p[i]*scale
    glm::vec3 apply(const glm::vec3& p) const {
        glm::vec3 out = offset;
        for (int i = 0; i < 3; ++i) out[img[i]] += static_cast<float>(sgn[i]) * p[i] * scale;
        return out;
    }
};

AxisXform composeXform(const AxisXform& outer, const AxisXform& inner) {
    AxisXform out;
    out.scale = outer.scale * inner.scale;
    for (int i = 0; i < 3; ++i) {
        out.img[i] = outer.img[inner.img[i]];
        out.sgn[i] = outer.sgn[inner.img[i]] * inner.sgn[i];
    }
    out.offset = outer.apply(inner.offset);
    return out;
}

// Snaps a quaternion to a signed axis permutation (rotation matrix entries
// within 1e-5 of 0/±1 — the float rounding of e.g. 90-degree euler angles);
// false for genuinely tilted rotations.
bool axisPermutation(const glm::quat& q, AxisXform& xf) {
    const glm::mat3 m = glm::mat3_cast(q);  // column c = image of local axis c
    bool seen[3] = {false, false, false};
    for (int c = 0; c < 3; ++c) {
        int best = -1;
        float bestAbs = 0.0f;
        for (int r = 0; r < 3; ++r) {
            const float a = std::fabs(m[c][r]);
            if (a > bestAbs) {
                bestAbs = a;
                best = r;
            }
        }
        if (std::fabs(bestAbs - 1.0f) > 1e-5f) return false;
        for (int r = 0; r < 3; ++r)
            if (r != best && std::fabs(m[c][r]) > 1e-5f) return false;
        if (seen[best]) return false;
        seen[best] = true;
        xf.img[c] = best;
        xf.sgn[c] = m[c][best] >= 0.0f ? 1 : -1;
    }
    return seen[0] && seen[1] && seen[2];
}

// One axis plane of a Box primitive (or one axis of a Sphere centre).
struct LatticePlane {
    int axis;
    float coord;
    const SdfNode* node;
    size_t order;  // first-appearance order — the stable tiebreak (pointers don't sort)
};

struct LatticeWalk {
    std::vector<LatticePlane> planes;    // Box face planes
    std::vector<LatticePlane> centers;   // Sphere centres, one entry per axis
    std::unordered_set<const SdfNode*> visitedIdentity;  // DAG sharing guard (identity xform)
    size_t skippedAnchors = 0;

    void walk(const SdfNode& n, const AxisXform& xf) {
        if (xf.isIdentity() && !visitedIdentity.insert(&n).second) return;
        switch (n.kind) {
            case SdfKind::Box:
                for (int a = 0; a < 3; ++a)
                    for (int s = -1; s <= 1; s += 2) {
                        const float coord = xf.offset[xf.img[a]] +
                                            static_cast<float>(xf.sgn[a] * s) * (n.size[a] * 0.5f) * xf.scale;
                        planes.push_back({xf.img[a], coord, &n, planes.size() + centers.size()});
                    }
                break;
            case SdfKind::Sphere: {
                const glm::vec3 c = xf.apply(glm::vec3(0.0f));
                for (int a = 0; a < 3; ++a)
                    centers.push_back({a, c[a], &n, planes.size() + centers.size()});
                break;
            }
            case SdfKind::Union:
            case SdfKind::UnionSmooth:
            case SdfKind::Subtract:
            case SdfKind::SubtractSmooth:
            case SdfKind::Intersect:
            case SdfKind::Grind:
                walk(*n.a, xf);
                walk(*n.b, xf);
                break;
            case SdfKind::Displace:
                walk(*n.a, xf);
                break;
            case SdfKind::Instance:
                for (const SdfInstanceAnchor& an : n.anchors) {
                    AxisXform local;
                    local.offset = an.pos;
                    local.scale = an.scale;
                    if (!axisPermutation(an.orient, local)) {
                        ++skippedAnchors;
                        continue;
                    }
                    walk(*n.a, composeXform(xf, local));
                }
                break;
            case SdfKind::Grid:
            case SdfKind::VoronoiCell:
                break;  // no analytic axis planes (a voxelized mesh / a half-plane cell)
        }
    }
};

// Dedup by (node, axis, coord), keeping first appearance: shared DAG children
// and repeated anchors would otherwise print the same plane twice.
void dedupPlanes(std::vector<LatticePlane>& v) {
    std::set<std::tuple<const SdfNode*, int, float>> seen;
    size_t w = 0;
    for (size_t i = 0; i < v.size(); ++i) {
        if (!seen.emplace(v[i].node, v[i].axis, v[i].coord).second) continue;
        v[w++] = v[i];
    }
    v.resize(w);
}

const char kAxisLetter[] = {'x', 'y', 'z'};

}  // namespace

bool probeLattice(const SdfNode& sdf, const std::vector<std::pair<std::string, std::string>>& params,
                  std::string& out, std::string& err) {
    const std::string* voxelP = findProbeParam(params, "voxel");
    if (!voxelP) {
        err = "lattice needs voxel=<cell size> (the voxel= you would pass to mesh_from_sdf)";
        return false;
    }
    float voxel = 0.0f;
    if (!parseFloatRaw(*voxelP, voxel) || !(voxel > 0.0f)) {
        err = "bad voxel value '" + *voxelP + "' (a positive number expected)";
        return false;
    }
    const SdfLattice lat = meshFromSdfLattice(sdf, voxel);
    out = "lattice[voxel=" + fmtG(voxel) + "]";
    if (lat.empty) {
        out += " (empty conservative bbox)\nwarns 0";
        return true;
    }
    out += " (dims " + std::to_string(lat.dims.x) + " " + std::to_string(lat.dims.y) + " " +
           std::to_string(lat.dims.z) + ", origin " + fmtVec3(lat.origin) + ", step " + fmtG(voxel) +
           (lat.axisOverflow ? ", axis overflow (>4096 voxels, E306)" : "") + ")";

    LatticeWalk w;
    w.walk(sdf, AxisXform{});
    dedupPlanes(w.planes);
    dedupPlanes(w.centers);
    std::stable_sort(w.planes.begin(), w.planes.end(),
                     [](const LatticePlane& a, const LatticePlane& b) {
                         return a.axis != b.axis ? a.axis < b.axis : a.coord < b.coord;
                     });
    std::stable_sort(w.centers.begin(), w.centers.end(),
                     [](const LatticePlane& a, const LatticePlane& b) {
                         return a.axis != b.axis ? a.axis < b.axis : a.coord < b.coord;
                     });

    std::vector<std::string> warns;
    std::unordered_set<std::string> seenWarns;
    const auto addWarn = [&](std::string s) {
        if (seenWarns.insert(s).second) warns.push_back(std::move(s));
    };
    const float faceThr = 0.05f * voxel;
    const auto nearLattice = [&](int axis, float coord, float& d) {
        const float rel = (coord - lat.origin[axis]) / voxel;
        const float nearest = lat.origin[axis] + std::round(rel) * voxel;
        d = std::fabs(coord - nearest);
        return d < faceThr;
    };
    for (const LatticePlane& pl : w.planes) {
        float d;
        if (nearLattice(pl.axis, pl.coord, d))
            addWarn("warn: face plane " + std::string(1, kAxisLetter[pl.axis]) + "=" + fmtG(pl.coord) + " is " +
                    fmtG(d) + " from lattice plane (threshold " + fmtG(faceThr) + ")");
    }
    for (const LatticePlane& pl : w.centers) {
        float d;
        if (nearLattice(pl.axis, pl.coord, d))
            addWarn("warn: sphere center " + std::string(1, kAxisLetter[pl.axis]) + "=" + fmtG(pl.coord) +
                    " is " + fmtG(d) + " from lattice plane (threshold " + fmtG(faceThr) + ")");
    }
    // Pairs of parallel planes of DIFFERENT primitives closer than 2 voxels —
    // a wall marching cubes cannot represent (coincident planes print d = 0).
    const float pairThr = 2.0f * voxel;
    for (size_t i = 0; i < w.planes.size(); ++i)
        for (size_t j = i + 1; j < w.planes.size(); ++j) {
            const LatticePlane& a = w.planes[i];
            const LatticePlane& b = w.planes[j];
            if (b.axis != a.axis || b.coord - a.coord >= pairThr) break;
            if (a.node == b.node) continue;
            addWarn("warn: parallel faces " + std::string(1, kAxisLetter[a.axis]) + "=" + fmtG(a.coord) +
                    " and " + std::string(1, kAxisLetter[b.axis]) + "=" + fmtG(b.coord) + " are " +
                    fmtG(b.coord - a.coord) + " apart (< 2*voxel)");
        }

    if (w.skippedAnchors > 0)
        out += "\nnote: " + std::to_string(w.skippedAnchors) +
               " instance anchor(s) skipped (non-axis-aligned rotation)";
    for (const std::string& s : warns) out += "\n" + s;
    out += "\nwarns " + std::to_string(warns.size());
    return true;
}

// --- aggregate=stats ------------------------------------------------------------

std::string probeAggregateStats(const std::vector<std::vector<ProbeStatsEntry>>& perInstance) {
    std::string out;
    // Labels in first-appearance order (per-instance sets are aligned).
    std::vector<std::string> labels;
    for (const auto& entries : perInstance)
        for (const ProbeStatsEntry& e : entries)
            if (std::find(labels.begin(), labels.end(), e.label) == labels.end()) labels.push_back(e.label);
    for (const std::string& label : labels) {
        double sum = 0.0;
        size_t k = 0;
        for (const auto& entries : perInstance)
            for (const ProbeStatsEntry& e : entries)
                if (e.label == label) {
                    sum += e.mean;
                    ++k;
                }
        const double mean = k > 0 ? sum / static_cast<double>(k) : 0.0;
        double sq = 0.0;
        for (const auto& entries : perInstance)
            for (const ProbeStatsEntry& e : entries)
                if (e.label == label) sq += (e.mean - mean) * (e.mean - mean);
        const double std = k > 0 ? std::sqrt(sq / static_cast<double>(k)) : 0.0;
        if (!out.empty()) out += "\n";
        out += label + ": mean " + fmtG(mean) + " \xc2\xb1 " + fmtG(std) + " across " +
               std::to_string(k) + " instances";
    }
    return out;
}

std::string probeAggregateCoverage(const std::vector<ProbeCoverage>& perInstance) {
    size_t t = 0, n = 0;
    std::string label;
    for (const ProbeCoverage& c : perInstance) {
        if (label.empty()) label = c.label;
        t += c.t;
        n += c.n;
    }
    ProbeCoverage pooled{label, t, n};
    return formatProbeCoverage(pooled) + " across " + std::to_string(perInstance.size()) + " instances";
}

std::string probeAggregateSchema(const std::vector<std::string>& perInstance) {
    std::string out;
    std::vector<std::pair<std::string, size_t>> groups;  // text -> count, first-appearance order
    for (const std::string& text : perInstance) {
        bool found = false;
        for (auto& [t, count] : groups)
            if (t == text) {
                ++count;
                found = true;
                break;
            }
        if (!found) groups.push_back({text, 1});
    }
    for (const auto& [text, count] : groups) {
        if (!out.empty()) out += "\n";
        out += text;
        if (count > 1) out += " x " + std::to_string(count) + " instances";
    }
    return out;
}

}  // namespace pgg
