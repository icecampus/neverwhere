// PggTool: CLI front-end for the PGG language (spec E0+E1).
//   PggTool check <file.pgg> [--json]   parse + lint, print diagnostics
//   PggTool fmt <file.pgg> [--check]    canonical format
//   PggTool ast <file.pgg>              dump the AST
//   PggTool run <file.pgg> [--param k=v]... [--output name]... [--obj <dir>]
//                      [--threads N] [--lib <dir>]... [--probe <spec>]... [--debug]
//                                       run the graph, print output summaries
//                                       (E6: probes/taps print inspector records)
//   PggTool docs <file.pgg> <symbol> [--lib <dir>]...
//                                       print a def's signature + docstring (§7.5)
// Exit codes: 0 ok, 1 diagnostics with errors, 2 usage/io failure.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <variant>

#include <pgg/eval.h>
#include <pgg/pgg.h>
#include <pgg/src/eval/builtins.h>  // realizeInstances for the --obj export
#include <pgg/src/eval/modules.h>   // import closure for docs of qualified symbols
#include <pgg/src/eval/sdf.h>       // sdf output summaries

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  PggTool check <file.pgg> [--json]   parse + lint, print diagnostics\n"
                 "  PggTool fmt <file.pgg> [-i|--check] canonical format (stdout, write-back, or diff-check)\n"
                 "  PggTool ast <file.pgg>              dump the AST\n"
                 "  PggTool run <file.pgg> [--param k=v]... [--output name]... [--obj <dir>]\n"
                 "                        [--threads N] [--lib <dir>]... [--probe <spec>]... [--debug]\n"
                 "                                      run the graph, print output summaries\n"
                 "                                      (--obj writes one Wavefront OBJ per geo output;\n"
                 "                                      --probe 'path:inspector[param=value,...]' inspects a\n"
                 "                                      binding without computing downstream nodes — probes\n"
                 "                                      without --output skip the declared outputs;\n"
                 "                                      --debug also fires the file's tap marks)\n"
                 "  PggTool docs <file.pgg> <symbol> [--lib <dir>]...\n"
                 "                                      print a def's signature + docstring (§7.5)\n");
}

std::string jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

int cmdCheck(const std::string& path, bool json) {
    pgg::Document doc = pgg::parseFile(path);
    if (json) {
        std::string out = "[";
        for (size_t i = 0; i < doc.diagnostics.size(); ++i) {
            const pgg::Diagnostic& d = doc.diagnostics[i];
            if (i) out += ",";
            out += "{\"code\":\"" + d.code + "\",\"line\":" + std::to_string(d.span.line) +
                   ",\"col\":" + std::to_string(d.span.col) + ",\"warning\":" +
                   (d.isWarning ? "true" : "false") + ",\"message\":\"" +
                   jsonEscape(d.message) + "\"";
            if (!d.hint.empty()) out += ",\"hint\":\"" + jsonEscape(d.hint) + "\"";
            out += "}";
        }
        out += "]\n";
        std::fputs(out.c_str(), stdout);
    } else {
        for (const pgg::Diagnostic& d : doc.diagnostics) {
            std::fputs(pgg::formatDiagnostic(d, path).c_str(), stdout);
            std::fputc('\n', stdout);
        }
        int errors = 0, warnings = 0;
        for (const pgg::Diagnostic& d : doc.diagnostics) (d.isWarning ? warnings : errors) += 1;
        std::printf("%s: %d error(s), %d warning(s)\n", errors ? "FAIL" : "OK", errors, warnings);
    }
    return doc.hasErrors() ? 1 : 0;
}

int cmdAst(const std::string& path) {
    pgg::Document doc = pgg::parseFile(path);
    for (const pgg::Diagnostic& d : doc.diagnostics) {
        std::fputs(pgg::formatDiagnostic(d, path).c_str(), stderr);
        std::fputc('\n', stderr);
    }
    if (doc.file) std::fputs(pgg::dumpAst(doc.file).c_str(), stdout);
    return doc.hasErrors() ? 1 : 0;
}

int cmdFmt(const std::string& path, bool inPlace, bool checkOnly) {    pgg::Document doc = pgg::parseFile(path);
    if (doc.hasErrors()) {
        for (const pgg::Diagnostic& d : doc.diagnostics) {
            if (!d.isWarning) {
                std::fputs(pgg::formatDiagnostic(d, path).c_str(), stderr);
                std::fputc('\n', stderr);
            }
        }
        return 1;
    }
    const std::string formatted = pgg::format(doc.file, doc.comments);
    if (checkOnly) {
        std::ifstream in(path, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        if (ss.str() == formatted) {
            std::printf("OK: %s is canonical\n", path.c_str());
            return 0;
        }
        std::printf("FAIL: %s is not canonical (run PggTool fmt -i)\n", path.c_str());
        return 1;
    }
    if (inPlace) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::fprintf(stderr, "cannot write %s\n", path.c_str());
            return 2;
        }
        out << formatted;
        return 0;
    }
    std::fputs(formatted.c_str(), stdout);
    return 0;
}

// CLI value parsing for --param k=v: bool / int / f32 / (vec) / string.
pgg::Value parseCliValue(const std::string& v) {
    if (v == "true") return pgg::Value(true);
    if (v == "false") return pgg::Value(false);
    if (v.size() >= 5 && v.front() == '(' && v.back() == ')') {
        std::vector<float> comps;
        std::stringstream ss(v.substr(1, v.size() - 2));
        std::string item;
        bool ok = true;
        while (std::getline(ss, item, ',')) {
            char* end = nullptr;
            const float f = std::strtof(item.c_str(), &end);
            if (end == item.c_str() || *end != '\0') ok = false;
            comps.push_back(f);
        }
        if (ok && comps.size() == 2) return pgg::Value(glm::vec2(comps[0], comps[1]));
        if (ok && comps.size() == 3) return pgg::Value(glm::vec3(comps[0], comps[1], comps[2]));
        if (ok && comps.size() == 4) return pgg::Value(glm::vec4(comps[0], comps[1], comps[2], comps[3]));
        return pgg::Value(v);
    }
    char* end = nullptr;
    const long long iv = std::strtoll(v.c_str(), &end, 10);
    if (end && *end == '\0' && end != v.c_str()) return pgg::Value(static_cast<int64_t>(iv));
    const float fv = std::strtof(v.c_str(), &end);
    if (end && *end == '\0' && end != v.c_str()) return pgg::Value(fv);
    return pgg::Value(v);
}

void printGeoSummary(const std::string& name, const pgg::Geo& geo) {
    std::printf("%s: geo<%s> points=%zu", name.c_str(), pgg::geoKindName(geo.kind), geo.pointCount());
    if (geo.kind == pgg::GeoKind::Mesh)
        std::printf(" corners=%zu faces=%zu", geo.cornerCount(), geo.faceCount());
    if (geo.kind == pgg::GeoKind::Instances && geo.instanceSources) {
        // Per-variant instance counts from the @variant stamp (default 0) and
        // the total realized potential (sum of source sizes per instance).
        std::vector<size_t> perVariant(geo.instanceSources->size(), 0);
        std::optional<pgg::ColumnData> variantCol =
            pgg::sampleAttrColumn(geo, "variant", pgg::Domain::Points);
        size_t realizedPoints = 0;
        for (size_t i = 0; i < geo.pointCount(); ++i) {
            int64_t v = 0;
            if (variantCol && variantCol->index() == 1)
                v = (*std::get<std::shared_ptr<const std::vector<int64_t>>>(*variantCol))[i];
            const size_t idx = static_cast<size_t>(
                std::clamp<int64_t>(v, 0, static_cast<int64_t>(geo.instanceSources->size()) - 1));
            perVariant[idx] += 1;
            realizedPoints += (*geo.instanceSources)[idx]->pointCount();
        }
        std::printf(" variants=%zu instances=[", geo.instanceSources->size());
        for (size_t i = 0; i < perVariant.size(); ++i)
            std::printf("%s%zu", i ? ", " : "", perVariant[i]);
        std::printf("] realized_points=%zu", realizedPoints);
    }
    if (geo.pointCount() > 0) {
        glm::vec3 mn, mx;
        pgg::geoBBox(geo, mn, mx);
        std::printf(" bbox=(%g, %g, %g)..(%g, %g, %g)", mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
    }
    std::printf("\n");
}

void printSdfSummary(const std::string& name, const pgg::SdfNode& sdf) {
    glm::vec3 mn, mx;
    sdf.conservativeBBox(mn, mx);
    std::printf("%s: sdf nodes=%zu", name.c_str(), pgg::sdfNodeCount(sdf));
    if (mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z)
        std::printf(" bbox=(%g, %g, %g)..(%g, %g, %g)", mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
    else
        std::printf(" bbox=(empty)");
    std::printf("\n");
}

// Domain the vec3 @Cd column lives on (spec §4.3 read order); nullopt when
// absent or not vec3.
std::optional<pgg::Domain> colorDomain(const pgg::Geo& geo) {
    for (pgg::Domain d : {pgg::Domain::Points, pgg::Domain::Corners, pgg::Domain::Faces, pgg::Domain::Detail}) {
        const pgg::AttrSet* attrs = geo.attrs(d);
        const pgg::AttrColumn* col = attrs ? attrs->find("Cd") : nullptr;
        if (!col) continue;
        return std::holds_alternative<std::shared_ptr<const std::vector<glm::vec3>>>(col->data)
                   ? std::optional<pgg::Domain>(d)
                   : std::nullopt;
    }
    return std::nullopt;
}

std::shared_ptr<const std::vector<glm::vec3>> vec3Column(const std::optional<pgg::ColumnData>& col, size_t count) {
    if (!col) return nullptr;
    const auto* vec = std::get_if<std::shared_ptr<const std::vector<glm::vec3>>>(&*col);
    if (!vec || !*vec || (*vec)->size() != count) return nullptr;
    return *vec;
}

std::shared_ptr<const std::vector<float>> f32Column(const std::optional<pgg::ColumnData>& col, size_t count) {
    if (!col) return nullptr;
    const auto* vec = std::get_if<std::shared_ptr<const std::vector<float>>>(&*col);
    if (!vec || !*vec || (*vec)->size() != count) return nullptr;
    return *vec;
}

bool hasAttr(const pgg::Geo& g, const char* name) {
    for (pgg::Domain d : {pgg::Domain::Points, pgg::Domain::Corners, pgg::Domain::Faces})
        if (const pgg::AttrSet* a = g.attrs(d); a && a->find(name)) return true;
    return false;
}

// Wavefront OBJ. Surface color @Cd (spec §4.3) goes out as the widely read
// `v x y z r g b` extension (Blender, MeshLab, Houdini). @Cd on points writes
// the welded mesh; on corners/faces/detail the mesh is unwelded (one vertex
// per corner) so face colors survive without bleeding into neighbours.
// Normals: stored point @N (or corner N from compute_normals flat) as `vn`.
bool writeObj(const std::string& path, const pgg::Geo& geo) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# PggTool run export\n";
    const std::optional<pgg::Domain> cdDomain = colorDomain(geo);
    const bool unweld = geo.kind == pgg::GeoKind::Mesh && cdDomain && *cdDomain != pgg::Domain::Points;
    const pgg::Domain vdom = unweld ? pgg::Domain::Corners : pgg::Domain::Points;
    const size_t vcount = geo.elementCount(vdom);
    const std::shared_ptr<const std::vector<glm::vec3>> P = pgg::samplePositions(geo, vdom);
    std::shared_ptr<const std::vector<glm::vec3>> Cd =
        cdDomain ? vec3Column(pgg::sampleAttrColumn(geo, "Cd", vdom), vcount) : nullptr;
    // Baked occlusion (@ao, v1.24) is multiplied into the exported color: OBJ
    // has no separate AO channel, and the viewer applies it the same way.
    if (const auto ao = hasAttr(geo, "ao") ? f32Column(pgg::sampleAttrColumn(geo, "ao", vdom), vcount) : nullptr) {
        std::vector<glm::vec3> shaded(vcount);
        for (size_t i = 0; i < vcount; ++i)
            shaded[i] = (Cd ? (*Cd)[i] : glm::vec3(0.66f, 0.64f, 0.61f)) * std::clamp((*ao)[i], 0.0f, 1.0f);
        Cd = std::make_shared<const std::vector<glm::vec3>>(std::move(shaded));
    }
    std::shared_ptr<const std::vector<glm::vec3>> N;
    if (geo.kind == pgg::GeoKind::Mesh) {
        const pgg::AttrSet* cattrs = geo.attrs(pgg::Domain::Corners);
        const pgg::AttrColumn* cornerN = cattrs ? cattrs->find("N") : nullptr;
        if (cornerN && unweld) N = vec3Column(cornerN->data, vcount);
        if (!N && geo.normals) N = pgg::sampleNormals(geo, vdom);
    }
    if (Cd) out << "# vertex colors: @Cd" << (cdDomain ? std::string(" on ") + pgg::domainName(*cdDomain) : std::string(" (neutral)"))
                << (hasAttr(geo, "ao") ? " x @ao" : "") << (unweld ? " (unwelded)" : "") << "\n";
    for (size_t i = 0; i < vcount; ++i) {
        const glm::vec3& p = (*P)[i];
        out << "v " << p.x << " " << p.y << " " << p.z;
        if (Cd) {
            const glm::vec3 c = glm::clamp((*Cd)[i], glm::vec3(0.0f), glm::vec3(1.0f));
            out << " " << c.x << " " << c.y << " " << c.z;
        }
        out << "\n";
    }
    if (N && N->size() == vcount)
        for (const glm::vec3& n : *N) out << "vn " << n.x << " " << n.y << " " << n.z << "\n";
    else
        N = nullptr;
    if (geo.kind == pgg::GeoKind::Mesh) {
        // Fan triangulation of polygon faces, 1-based indices.
        auto vertex = [&](int32_t c) { return unweld ? c + 1 : (*geo.cornerVerts)[c] + 1; };
        auto emit = [&](int32_t c) {
            const int idx = vertex(c);
            out << " " << idx;
            if (N) out << "//" << idx;
        };
        for (size_t f = 0; f < geo.faceCount(); ++f) {
            const int32_t begin = (*geo.faceOffsets)[f];
            const int32_t end = (*geo.faceOffsets)[f + 1];
            for (int32_t c = begin + 1; c + 1 < end; ++c) {
                out << "f";
                emit(begin);
                emit(c);
                emit(c + 1);
                out << "\n";
            }
        }
    }
    return static_cast<bool>(out);
}

int cmdRun(const std::string& path, const std::vector<std::pair<std::string, std::string>>& params,
           const std::vector<std::string>& outputs, const std::string& objDir, unsigned threads,
           const std::vector<std::string>& libRoots, const std::vector<std::string>& probes, bool debug) {
    pgg::RunParams rp;
    for (const auto& [k, v] : params) rp.values.push_back({k, parseCliValue(v)});
    rp.threads = threads;
    rp.importRoots = libRoots;
    rp.probes = probes;
    rp.debug = debug;
    pgg::RunResult result = pgg::runFile(path, rp, outputs);
    for (const pgg::Diagnostic& d : result.diagnostics) {
        std::fputs(pgg::formatDiagnostic(d, path).c_str(), stderr);
        std::fputc('\n', stderr);
    }
    if (result.hasErrors()) return 1;
    for (const pgg::RunOutput& o : result.outputs) {
        if (pgg::valueBase(o.value) == pgg::ScalarType::Geo) {
            printGeoSummary(o.name, *pgg::asGeo(o.value));
        } else if (pgg::valueBase(o.value) == pgg::ScalarType::Sdf) {
            printSdfSummary(o.name, *pgg::asSdf(o.value));
        } else {
            std::printf("%s: %s = %s\n", o.name.c_str(), pgg::scalarName(pgg::valueBase(o.value)),
                        pgg::valueToString(o.value).c_str());
        }
    }
    // E6 probe/tap records (§9), after the outputs (or standalone in a
    // probe-only run), before the run stats line.
    for (const pgg::ProbeRecord& pr : result.probes)
        std::printf("%s %s: %s\n", pr.origin.c_str(), pr.path.c_str(), pr.text.c_str());
    std::printf("run: %zu output(s), %llu field evaluation(s)\n", result.outputs.size(),
                (unsigned long long)result.stats.fieldsEvaluated);
    std::printf("profile: %016llx threads: %u cache: %llu hit(s) %llu miss(es)\n",
                (unsigned long long)result.stats.profileId, result.stats.threadsUsed,
                (unsigned long long)result.stats.cacheHits, (unsigned long long)result.stats.cacheMisses);
    if (!objDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(objDir, ec);
        for (const pgg::RunOutput& o : result.outputs) {
            if (pgg::valueBase(o.value) == pgg::ScalarType::Sdf) {
                std::printf("skipping %s: sdf outputs are not exported; mesh them with mesh_from_sdf\n",
                            o.name.c_str());
                continue;
            }
            if (pgg::valueBase(o.value) != pgg::ScalarType::Geo) continue;
            const pgg::Geo& geo = *pgg::asGeo(o.value);
            const std::string objPath = objDir + "/" + o.name + ".obj";
            // Instances have no polygons of their own: the export realizes
            // them first (the only place instances get expensive, §8.8).
            if (geo.kind == pgg::GeoKind::Instances) {
                pgg::GeoPtr realized = pgg::realizeInstances(geo);
                if (!realized || !writeObj(objPath, *realized)) {
                    std::fprintf(stderr, "cannot write %s\n", objPath.c_str());
                    return 2;
                }
                std::printf("wrote %s (realized from geo<instances>)\n", objPath.c_str());
                continue;
            }
            if (!writeObj(objPath, geo)) {
                std::fprintf(stderr, "cannot write %s\n", objPath.c_str());
                return 2;
            }
            std::printf("wrote %s\n", objPath.c_str());
        }
    }
    return 0;
}
// --- docs (spec §7.5): a def's signature as written + its docstring ---------

std::string typeRefText(const pgg::TypeRef* t) {
    if (!t) return "?";
    std::string out = t->base;
    if (t->base == "geo" && !t->geoKind.empty()) out += "<" + t->geoKind + ">";
    if (t->base == "field" && t->arg) out = "field<" + typeRefText(t->arg) + ">";
    if (t->base == "enum" && !t->enumValues.empty()) {
        out = "enum {";
        for (size_t i = 0; i < t->enumValues.size(); ++i) out += (i ? ", " : "") + t->enumValues[i];
        out += "}";
    }
    if (t->optional) out += "?";
    if (t->list) out += "[]";
    return out;
}

// Minimal expression rendering for signature defaults (literals and simple
// arithmetic is all the grammar allows there).
std::string exprText(const pgg::Expr* e) {
    if (!e) return "?";
    switch (e->kind) {
        case pgg::NodeKind::NumberLit: return static_cast<const pgg::NumberLit*>(e)->text;
        case pgg::NodeKind::StringLit:
            return "\"" + static_cast<const pgg::StringLit*>(e)->value + "\"";
        case pgg::NodeKind::BoolLit:
            return static_cast<const pgg::BoolLit*>(e)->value ? "true" : "false";
        case pgg::NodeKind::NoneLit: return "none";
        case pgg::NodeKind::EnumLit: return static_cast<const pgg::EnumLit*>(e)->name;
        case pgg::NodeKind::Ident: return static_cast<const pgg::Ident*>(e)->name;
        case pgg::NodeKind::AttrRef: return "@" + static_cast<const pgg::AttrRef*>(e)->name;
        case pgg::NodeKind::VecLit: {
            std::string out = "(";
            const auto* v = static_cast<const pgg::VecLit*>(e);
            for (size_t i = 0; i < v->elems.size(); ++i) out += (i ? ", " : "") + exprText(v->elems[i]);
            return out + ")";
        }
        case pgg::NodeKind::ListLit: {
            std::string out = "[";
            const auto* l = static_cast<const pgg::ListLit*>(e);
            for (size_t i = 0; i < l->elems.size(); ++i) out += (i ? ", " : "") + exprText(l->elems[i]);
            return out + "]";
        }
        case pgg::NodeKind::Paren: return "(" + exprText(static_cast<const pgg::Paren*>(e)->inner) + ")";
        case pgg::NodeKind::Unary: {
            const auto* u = static_cast<const pgg::Unary*>(e);
            return u->op + exprText(u->operand);
        }
        case pgg::NodeKind::Binary: {
            const auto* b = static_cast<const pgg::Binary*>(e);
            return exprText(b->lhs) + " " + b->op + " " + exprText(b->rhs);
        }
        case pgg::NodeKind::Ternary: {
            const auto* t = static_cast<const pgg::Ternary*>(e);
            return exprText(t->cond) + " ? " + exprText(t->thenExpr) + " : " + exprText(t->elseExpr);
        }
        case pgg::NodeKind::Call: {
            const auto* c = static_cast<const pgg::Call*>(e);
            std::string out;
            for (const std::string& p : c->path) out += (out.empty() ? "" : ".") + p;
            out += "(";
            for (size_t i = 0; i < c->args.size(); ++i) {
                out += i ? ", " : "";
                if (c->args[i].hasName) out += c->args[i].name + " = ";
                out += exprText(c->args[i].value);
            }
            return out + ")";
        }
        default: return "?";
    }
}

std::string signatureText(const pgg::Def* d) {
    std::string out = "def " + d->name + "(";
    for (size_t i = 0; i < d->params.size(); ++i) {
        const pgg::DefParam& p = d->params[i];
        out += (i ? ", " : "") + p.name + ": " + typeRefText(p.type);
        if (p.hasDefault) out += " = " + exprText(p.def);
    }
    out += ") -> (";
    for (size_t i = 0; i < d->outputs.size(); ++i) {
        const pgg::OutDecl& o = d->outputs[i];
        out += (i ? ", " : "") + o.name + ": " + typeRefText(o.type);
    }
    return out + ")";
}

void printDocCard(const pgg::Def* d) {
    std::puts(signatureText(d).c_str());
    if (d->hasDoc) {
        std::string doc = d->docstring;
        // Docstrings are written as one indented block; dedent line-by-line.
        std::stringstream ss(doc);
        std::string line;
        while (std::getline(ss, line)) {
            const size_t first = line.find_first_not_of(" \t");
            std::puts((first == std::string::npos ? "" : line.substr(first)).c_str());
        }
    } else {
        std::puts("(no docstring)");
    }
}

int cmdDocs(const std::string& path, const std::string& symbol, const std::vector<std::string>& libRoots) {
    pgg::Document doc = pgg::parseFile(path);
    for (const pgg::Diagnostic& d : doc.diagnostics) {
        if (!d.isWarning) {
            std::fputs(pgg::formatDiagnostic(d, path).c_str(), stderr);
            std::fputc('\n', stderr);
        }
    }
    if (!doc.file) return 1;

    const pgg::Def* found = nullptr;
    const size_t dot = symbol.rfind('.');
    if (dot == std::string::npos) {
        for (const pgg::Node* item : doc.file->items) {
            if (item->kind != pgg::NodeKind::Def) continue;
            const auto* d = static_cast<const pgg::Def*>(item);
            if (d->name == symbol) found = d;
        }
        if (!found) {
            std::fprintf(stderr, "no def '%s' in %s\n", symbol.c_str(), path.c_str());
            return 1;
        }
    } else {
        // Qualified symbol: resolve through the file's import closure (same
        // roots as a run: --lib + the file's own directory).
        const std::string ns = symbol.substr(0, dot);
        const std::string name = symbol.substr(dot + 1);
        std::vector<std::string> roots = libRoots;
        const std::string dir = std::filesystem::path(path).parent_path().string();
        if (!dir.empty()) roots.push_back(dir);
        std::vector<pgg::Diagnostic> diags;
        pgg::ModuleClosure closure = pgg::loadModuleClosure(*doc.file, roots, diags);
        for (const pgg::Diagnostic& d : diags) {
            std::fputs(pgg::formatDiagnostic(d, path).c_str(), stderr);
            std::fputc('\n', stderr);
        }
        bool errors = false;
        for (const pgg::Diagnostic& d : diags) errors = errors || !d.isWarning;
        if (errors) return 1;
        if (auto it = closure.mainNamespaces.find(ns); it != closure.mainNamespaces.end()) {
            if (auto d = it->second->defs.find(name); d != it->second->defs.end()) found = d->second;
        }
        if (!found) {
            std::fprintf(stderr, "unknown qualified symbol '%s' (E505)\n", symbol.c_str());
            return 1;
        }
    }
    printDocCard(found);
    return 0;
}


}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    const std::string cmd = argv[1];
    const std::string path = argv[2];
    if (cmd == "docs") {
        if (argc < 4) {
            usage();
            return 2;
        }
        const std::string symbol = argv[3];
        std::vector<std::string> libRoots;
        for (int i = 4; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--lib" && i + 1 < argc) {
                libRoots.push_back(argv[++i]);
            } else if (a.rfind("--lib=", 0) == 0) {
                libRoots.push_back(a.substr(6));
            } else {
                usage();
                return 2;
            }
        }
        return cmdDocs(path, symbol, libRoots);
    }
    if (cmd == "run") {
        std::vector<std::pair<std::string, std::string>> params;
        std::vector<std::string> outputs;
        std::vector<std::string> libRoots;
        std::vector<std::string> probes;
        std::string objDir;
        unsigned threads = 0;  // 0 = hardware concurrency (RunParams default)
        bool debug = false;
        for (int i = 3; i < argc; ++i) {
            const std::string a = argv[i];
            auto takeValue = [&](const std::string& flag, std::string& out) -> bool {
                if (a == flag && i + 1 < argc) {
                    out = argv[++i];
                    return true;
                }
                if (a.rfind(flag + "=", 0) == 0) {
                    out = a.substr(flag.size() + 1);
                    return true;
                }
                return false;
            };
            std::string v;
            if (takeValue("--param", v)) {
                const size_t eq = v.find('=');
                if (eq == std::string::npos) {
                    usage();
                    return 2;
                }
                params.push_back({v.substr(0, eq), v.substr(eq + 1)});
            } else if (takeValue("--output", v)) {
                outputs.push_back(v);
            } else if (takeValue("--obj", v)) {
                objDir = v;
            } else if (takeValue("--threads", v)) {
                threads = static_cast<unsigned>(std::strtoul(v.c_str(), nullptr, 10));
            } else if (takeValue("--lib", v)) {
                libRoots.push_back(v);
            } else if (takeValue("--probe", v)) {
                probes.push_back(v);
            } else if (a == "--debug") {
                debug = true;
            } else {
                usage();
                return 2;
            }
        }
        return cmdRun(path, params, outputs, objDir, threads, libRoots, probes, debug);
    }
    bool json = false, inPlace = false, checkOnly = false;
    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--json") {
            json = true;
        } else if (a == "-i") {
            inPlace = true;
        } else if (a == "--check") {
            checkOnly = true;
        } else {
            usage();
            return 2;
        }
    }
    if (cmd == "check") return cmdCheck(path, json);
    if (cmd == "ast") return cmdAst(path);
    if (cmd == "fmt") return cmdFmt(path, inPlace, checkOnly);
    usage();
    return 2;
}
