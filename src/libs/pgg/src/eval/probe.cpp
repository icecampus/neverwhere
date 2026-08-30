#include "../../pch.h"

#include "probe.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

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
    // instance index (`make_rock[1]`) is part of the path.
    if (!head.empty() && head.back() == ']') {
        const size_t open = head.rfind('[');
        if (open != std::string::npos) {
            const std::string content = head.substr(open + 1, head.size() - open - 2);
            bool looksLikeParams = true;
            std::vector<std::pair<std::string, std::string>> pairs;
            size_t pos = 0;
            do {
                const size_t comma = content.find(',', pos);
                const std::string pair = content.substr(pos, comma == std::string::npos ? comma : comma - pos);
                const size_t eq = pair.find('=');
                if (eq == std::string::npos || eq == 0) {
                    looksLikeParams = false;
                    break;
                }
                pairs.push_back({pair.substr(0, eq), pair.substr(eq + 1)});
                pos = comma == std::string::npos ? content.size() : comma + 1;
            } while (pos < content.size());
            if (looksLikeParams && !pairs.empty()) {
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
                    } else if (name == "aggregate") {
                        if (value != "stats") {
                            err = "bad aggregate value '" + value + "' (only aggregate=stats is defined)";
                            return false;
                        }
                        out.aggregate = true;
                    } else {
                        err = "unknown probe parameter '" + name + "' (limit|aggregate)";
                        return false;
                    }
                }
                head = head.substr(0, open);
            }
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
        out.inspector != "coverage" && out.inspector != "table") {
        err = "unknown inspector '" + out.inspector + "' (schema|stats|coverage|table)";
        return false;
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

std::string probeGeoTable(const Geo& g, int limit) {
    const size_t n = g.pointCount();
    const size_t k = std::min(n, static_cast<size_t>(std::max(limit, 0)));
    std::string out = "table[limit=" + std::to_string(limit) + "] (first " + std::to_string(k) + " of " +
                      std::to_string(n) + " by @index)";
    // Cols: @P + point attributes sorted by name (@N listed among them).
    ColumnData positions = g.positions;
    std::map<std::string, ColumnData> cols;
    if (const AttrSet* attrs = g.attrs(Domain::Points))
        for (const auto& [name, col] : attrs->columns) cols[name] = col.data;
    if (g.normals) cols["N"] = g.normals;
    for (size_t i = 0; i < k; ++i) {
        out += "\n" + std::to_string(i) + ": @P=" + columnElementText(positions, i);
        for (const auto& [name, col] : cols) out += ", " + name + "=" + columnElementText(col, i);
    }
    return out;
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
