// PggTool: CLI front-end for the PGG language (spec E0+E1).
//   PggTool check <file.pgg> [--json]   parse + lint + static stage (imports,
//                                       expansion, typecheck — no graph run),
//                                       print diagnostics
//   PggTool check --explain <code>      diagnostic code reference card (§11.2)
//   PggTool fmt <file.pgg> [--check]    canonical format
//   PggTool ast <file.pgg>              dump the AST
//   PggTool run <file.pgg> [--param k=v]... [--output name]... [--obj <dir>]
//                      [--obj-split-groups] [--obj-color=check] [--threads N]
//                      [--lib <dir>]... [--probe <spec>]... [--debug]
//                      [--fingerprint] [--json] [--profile] [--eval '<expr>' --on <binding>]
//                      [--update-goldens]
//                                       run the graph, print output summaries
//                                       (E6: probes/taps print inspector records;
//                                       --fingerprint adds one structural hash per
//                                       output, --json prints the report as JSON;
//                                       --profile adds a per-binding wall-time table,
//                                       --eval evaluates an ad-hoc field on a binding;
//                                       --update-goldens rewrites the output
//                                       fingerprints golden file, C3)
//   PggTool diff <a.pgg> [<b.pgg>] [--output name]... [--lib <dir>]... [--json]
//                      [--baseline <fpfile>]
//                                       compare two runs (or a run against a saved
//                                       --fingerprint report) output by output
//   PggTool docs <file.pgg> <symbol> [--lib <dir>]...
//                                       print a def's signature + docstring (§7.5)
//   PggTool docs builtin <name>         builtin card: signature + summary + example (§8)
//   PggTool docs builtins [--group g]   the builtin catalog, one signature per line
// Exit codes: 0 ok (diff: all compared outputs identical), 1 diagnostics with
// errors (diff: outputs differ), 2 usage/io failure.

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include <pgg/eval.h>
#include <pgg/pgg.h>
#include <pgg/src/eval/builtin_docs.h>  // findBuiltinDoc/allBuiltinDocs for docs builtin[s]
#include <pgg/src/eval/builtins.h>      // builtinRegistry/realizeInstances for docs and --obj
#include <pgg/src/eval/docs_lookup.h>   // findDef/signatureText for docs
#include <pgg/src/eval/error_cards.h>   // findErrorCard for check --explain
#include <pgg/src/eval/expand.h>        // expandProgram for the check static stage
#include <pgg/src/eval/fingerprint.h>   // fingerprintValue for --fingerprint/diff
#include <pgg/src/eval/geo_diff.h>      // diffGeo/formatGeoDiff for diff
#include <pgg/src/eval/modules.h>       // loadModuleClosure for the check static stage
#include <pgg/src/eval/obj_export.h>    // writeObj for the --obj export
#include <pgg/src/eval/sdf.h>           // sdf output summaries
#include <pgg/src/eval/typecheck.h>     // typecheckFlat for the check static stage

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  PggTool check <file.pgg> [--json]   parse + lint + static stage (imports,\n"
                 "                                      expansion, typecheck — no graph run),\n"
                 "                                      print diagnostics\n"
                 "  PggTool check --explain <code>      print the reference card of a diagnostic\n"
                 "                                      code (meaning, typical causes, example + fix)\n"
                 "  PggTool fmt <file.pgg> [-i|--check] canonical format (stdout, write-back, or diff-check)\n"
                 "  PggTool ast <file.pgg>              dump the AST\n"
                 "  PggTool run <file.pgg> [--param k=v]... [--output name]... [--obj <dir>]\n"
                 "                        [--obj-split-groups] [--obj-color=check] [--threads N]\n"
                 "                        [--lib <dir>]... [--probe <spec>]... [--debug]\n"
                 "                        [--fingerprint] [--json] [--profile]\n"
                 "                        [--eval '<expr>' --on <binding>] [--update-goldens]\n"
                 "                                      run the graph, print output summaries\n"
                 "                                      (--obj writes one Wavefront OBJ per geo output;\n"
                 "                                      --obj-split-groups splits the export into one OBJ\n"
                 "                                      per faces-group (<output>.<group>.obj, faces in no\n"
                 "                                      group -> <output>._nogroup.obj);\n"
                 "                                      --obj-color=check repaints problem faces:\n"
                 "                                      degenerate red, nonmanifold yellow, boundary blue;\n"
                 "                                      --probe 'path:inspector[param=value,...]' inspects a\n"
                 "                                      binding without computing downstream nodes — probes\n"
                 "                                      without --output skip the declared outputs;\n"
                 "                                      --debug also fires the file's tap marks;\n"
                 "                                      --fingerprint prints one structural hash per output\n"
                 "                                      (the baseline format of diff --baseline);\n"
                 "                                      --json prints the whole report as one JSON document;\n"
                 "                                      --profile adds the per-binding wall-time table\n"
                 "                                      (exclusive ms, top-20 + total; also in the JSON stats);\n"
                 "                                      --eval '<expr>' --on <binding> evaluates expr as a\n"
                 "                                      field on the points of the on-geometry (idents resolve\n"
                 "                                      to the file's bindings, @attrs to its attributes),\n"
                 "                                      materializes it as @__eval and prints its stats —\n"
                 "                                      outputs are computed as usual, --eval never narrows\n"
                 "                                      the run; --on takes the probe-path syntax;\n"
                 "                                      --update-goldens rewrites src/tests/pgg/goldens/\n"
                 "                                      <basename>.fp (path relative to the cwd — run from\n"
                 "                                      the repo root) with the fingerprints of this run;\n"
                 "                                      a failed run writes nothing)\n"
                 "  PggTool diff <a.pgg> [<b.pgg>] [--output name]... [--lib <dir>]... [--json]\n"
                 "                        [--baseline <fpfile>]\n"
                 "                                      compare two runs output by output: equal\n"
                 "                                      structural fingerprints -> identical, otherwise a\n"
                 "                                      kind/counts/bbox/+-attr/+-group table with ΔP stats;\n"
                 "                                      --baseline compares a.pgg against a saved\n"
                 "                                      run --fingerprint report instead of b.pgg\n"
                 "                                      (exit 0 identical, 1 different/run errors, 2 usage/io)\n"
                 "  PggTool docs <file.pgg> <symbol> [--lib <dir>]...\n"
                 "                                      print a def's signature + docstring (§7.5)\n"
                 "  PggTool docs builtin <name>         print a builtin's card: signature (from the\n"
                 "                                      live registry) + summary + example (§8)\n"
                 "  PggTool docs builtins [--group <g>] list the builtin catalog, one signature\n"
                 "                                      per line (groups: sources, transforms, rng,\n"
                 "                                      fields, expr, groups, attributes, topology,\n"
                 "                                      scatter, aggregators, sdf, fracture, deferred)\n");
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

// --- check --explain (agent_tooling_plan D2): the reference card of one ----
// diagnostic code (data: pgg::findErrorCard, spec §11.2).

void printIndented(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) std::printf("  %s\n", line.c_str());
}

int cmdExplain(const std::string& code) {
    const pgg::ErrorCard* card = pgg::findErrorCard(code);
    if (!card) {
        std::fprintf(stderr, "unknown diagnostic code '%s'\nknown codes:", code.c_str());
        for (const std::string& c : pgg::allErrorCodes()) std::fprintf(stderr, " %s", c.c_str());
        std::fprintf(stderr, "\n");
        return 1;
    }
    std::printf("%s - %s\n\n", card->code.c_str(), card->title.c_str());
    std::printf("Meaning: %s\n\n", card->meaning.c_str());
    std::printf("Typical causes:\n");
    for (const std::string& c : card->causes) std::printf("  - %s\n", c.c_str());
    std::printf("\nBad:\n");
    printIndented(card->exampleBad);
    std::printf("\nFix:\n");
    printIndented(card->exampleFix);
    return 0;
}

bool diagsHaveErrors(const std::vector<pgg::Diagnostic>& diags) {
    for (const pgg::Diagnostic& d : diags)
        if (!d.isWarning) return true;
    return false;
}

int cmdCheck(const std::string& path, bool json) {
    pgg::Document doc = pgg::parseFile(path);
    // F5 (agent_tooling_plan): check runs the full static pipeline — the
    // prefix of pgg::run / the viewer's load (import closure -> expansion ->
    // typecheck) without Engine::run, so static schema errors (E302/E305,
    // E609 through def boundaries) surface here in milliseconds. E604 stays a
    // launch error: check assumes every declared param gets bound at launch.
    std::vector<pgg::Diagnostic> diags = doc.diagnostics;
    if (doc.file && !doc.hasErrors()) {
        std::vector<std::string> importRoots;
        const std::string dir = std::filesystem::path(path).parent_path().string();
        if (!dir.empty()) importRoots.push_back(dir);  // like runFile (§7.6)
        pgg::ModuleClosure closure;
        const pgg::ModuleClosure* closurePtr = nullptr;
        if (pgg::hasImports(*doc.file)) {
            closure = pgg::loadModuleClosure(*doc.file, importRoots, diags);
            closurePtr = &closure;
        }
        pgg::FlatProgram flat = pgg::expandProgram(*doc.file, closurePtr, diags);
        if (!diagsHaveErrors(diags)) {
            std::vector<std::string> boundParams;
            for (const pgg::Node* item : flat.file->items)
                if (item->kind == pgg::NodeKind::ParamDecl)
                    boundParams.push_back(static_cast<const pgg::ParamDecl*>(item)->name);
            std::vector<size_t> runtimeContracts;
            pgg::typecheckFlat(flat, boundParams, diags, runtimeContracts);
        }
    }
    if (json) {
        std::string out = "[";
        for (size_t i = 0; i < diags.size(); ++i) {
            const pgg::Diagnostic& d = diags[i];
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
        for (const pgg::Diagnostic& d : diags) {
            std::fputs(pgg::formatDiagnostic(d, path).c_str(), stdout);
            std::fputc('\n', stdout);
        }
        int errors = 0, warnings = 0;
        for (const pgg::Diagnostic& d : diags) (d.isWarning ? warnings : errors) += 1;
        std::printf("%s: %d error(s), %d warning(s)\n", errors ? "FAIL" : "OK", errors, warnings);
    }
    return diagsHaveErrors(diags) ? 1 : 0;
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

// The geo detail text after "name: " (geo<kind> points=N [corners/faces |
// instances breakdown] [bbox=...]); printGeoSummary prints it with the prefix.
std::string geoSummaryText(const pgg::Geo& geo) {
    std::ostringstream out;
    out << "geo<" << pgg::geoKindName(geo.kind) << "> points=" << geo.pointCount();
    if (geo.kind == pgg::GeoKind::Mesh)
        out << " corners=" << geo.cornerCount() << " faces=" << geo.faceCount();
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
        out << " variants=" << geo.instanceSources->size() << " instances=[";
        for (size_t i = 0; i < perVariant.size(); ++i) out << (i ? ", " : "") << perVariant[i];
        out << "] realized_points=" << realizedPoints;
    }
    if (geo.pointCount() > 0) {
        glm::vec3 mn, mx;
        pgg::geoBBox(geo, mn, mx);
        char buf[128];
        std::snprintf(buf, sizeof(buf), " bbox=(%g, %g, %g)..(%g, %g, %g)", mn.x, mn.y, mn.z, mx.x,
                      mx.y, mx.z);
        out << buf;
    }
    return out.str();
}

void printGeoSummary(const std::string& name, const pgg::Geo& geo) {
    std::printf("%s: %s\n", name.c_str(), geoSummaryText(geo).c_str());
}

std::string sdfSummaryText(const pgg::SdfNode& sdf) {
    glm::vec3 mn, mx;
    sdf.conservativeBBox(mn, mx);
    std::ostringstream out;
    out << "sdf nodes=" << pgg::sdfNodeCount(sdf);
    char buf[128];
    if (mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z)
        std::snprintf(buf, sizeof(buf), " bbox=(%g, %g, %g)..(%g, %g, %g)", mn.x, mn.y, mn.z, mx.x,
                      mx.y, mx.z);
    else
        std::snprintf(buf, sizeof(buf), " bbox=(empty)");
    out << buf;
    return out.str();
}

void printSdfSummary(const std::string& name, const pgg::SdfNode& sdf) {
    std::printf("%s: %s\n", name.c_str(), sdfSummaryText(sdf).c_str());
}

// "geo<mesh>" for geo payloads, scalarName otherwise (kind field of --json).
std::string valueKindName(const pgg::Value& v) {
    if (pgg::valueBase(v) == pgg::ScalarType::Geo)
        return std::string("geo<") + pgg::geoKindName(pgg::asGeo(v)->kind) + ">";
    return pgg::scalarName(pgg::valueBase(v));
}

std::string hex16(uint64_t v) {
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long)v);
    return buf;
}

// The human summary text after "name: " for any value (same text cmdRun
// prints in non-JSON mode).
std::string outputSummaryText(const pgg::Value& v) {
    if (pgg::valueBase(v) == pgg::ScalarType::Geo) return geoSummaryText(*pgg::asGeo(v));
    if (pgg::valueBase(v) == pgg::ScalarType::Sdf) return sdfSummaryText(*pgg::asSdf(v));
    return std::string(pgg::scalarName(pgg::valueBase(v))) + " = " + pgg::valueToString(v);
}

// Diagnostic object of check --json / run --json / diff --json (hint only when
// present — the format cmdCheck established).
nlohmann::ordered_json diagToJson(const pgg::Diagnostic& d) {
    nlohmann::ordered_json j;
    j["code"] = d.code;
    j["line"] = d.span.line;
    j["col"] = d.span.col;
    j["warning"] = d.isWarning;
    j["message"] = d.message;
    if (!d.hint.empty()) j["hint"] = d.hint;
    return j;
}

nlohmann::ordered_json diagnosticsToJson(const std::vector<pgg::Diagnostic>& ds) {
    nlohmann::ordered_json out = nlohmann::ordered_json::array();
    for (const pgg::Diagnostic& d : ds) out.push_back(diagToJson(d));
    return out;
}

nlohmann::ordered_json statsToJson(const pgg::RunStats& s) {
    nlohmann::ordered_json j;
    j["fieldsEvaluated"] = s.fieldsEvaluated;
    j["cacheHits"] = s.cacheHits;
    j["cacheMisses"] = s.cacheMisses;
    j["threadsUsed"] = s.threadsUsed;
    j["profileId"] = hex16(s.profileId);
    // E-profile: per-binding wall times, sorted by ms desc (name asc on ties).
    // Empty unless the run had profiling on (run --profile / the viewer).
    nlohmann::ordered_json prof = nlohmann::ordered_json::array();
    for (const pgg::BindingProfile& b : pgg::profileByTime(s.profile)) {
        nlohmann::ordered_json jb;
        jb["name"] = b.name;
        jb["ms"] = b.ms;
        jb["fieldEvals"] = b.fieldEvals;
        jb["cacheHit"] = b.cacheHit;
        prof.push_back(std::move(jb));
    }
    j["profile"] = std::move(prof);
    return j;
}

// run --profile (agent_tooling_plan E): top-20 bindings by exclusive wall
// time, then the total over every evaluated binding (the exclusive column
// sums; nested pulls are on their own rows).
void printProfileTable(const pgg::RunStats& stats) {
    const std::vector<pgg::BindingProfile> rows = pgg::profileByTime(stats.profile);
    std::printf("profile: top-20 by time:\n");
    double totalMs = 0.0;
    for (const pgg::BindingProfile& b : rows) totalMs += b.ms;
    for (size_t i = 0; i < rows.size() && i < 20; ++i) {
        const pgg::BindingProfile& b = rows[i];
        if (b.cacheHit) {
            std::printf("%10.1f ms  %s (cache hit)\n", b.ms, b.name.c_str());
        } else {
            std::printf("%10.1f ms  %s (%llu field evals)\n", b.ms, b.name.c_str(),
                        (unsigned long long)b.fieldEvals);
        }
    }
    std::printf("%10.1f ms  total\n", totalMs);
}

// fingerprintValue lifted to optional: nullopt for payloads without a
// structural hash (sdf / compiled fields).
std::optional<uint64_t> outputFingerprint(const pgg::Value& v) {
    uint64_t fp = 0;
    if (!pgg::fingerprintValue(v, fp)) return std::nullopt;
    return fp;
}

// C3 (agent_tooling_plan): (re)writes the golden fingerprint file of a run —
// src/tests/pgg/goldens/<basename>.fp, the path relative to the cwd (run from
// the repo root; a missing directory is an error, we never create the tree
// elsewhere by accident). The content is exactly the run --fingerprint block
// (plus comment lines the diff --baseline parser skips), so a golden doubles
// as a --baseline file. Only the file's own golden is touched; an unchanged
// file is not rewritten (mtime/git stay clean). Human notes go to stdout, or
// to stderr in --json mode (stdout carries the JSON document there).
int writeGoldenFile(const std::string& path,
                    const std::vector<std::pair<std::string, std::string>>& params,
                    const pgg::RunResult& result, bool json) {
    const std::string dir = "src/tests/pgg/goldens";
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec)) {
        std::fprintf(stderr, "--update-goldens: %s is not a directory (run from the repo root)\n",
                     dir.c_str());
        return 2;
    }
    const std::string outPath = dir + "/" + std::filesystem::path(path).stem().string() + ".fp";
    std::ostringstream content;
    content << "# pgg golden, updated by PggTool run --update-goldens\n";
    if (!params.empty()) {
        content << "# params:";
        for (const auto& [k, v] : params) content << " " << k << "=" << pgg::valueToString(parseCliValue(v));
        content << "\n";
    }
    size_t n = 0;
    for (const pgg::RunOutput& o : result.outputs) {
        const std::optional<uint64_t> fp = outputFingerprint(o.value);
        if (fp) {
            content << "fingerprint " << o.name << ": " << hex16(*fp) << "\n";
            ++n;
        } else {
            content << "fingerprint " << o.name << ": - (no structural fingerprint)\n";
        }
    }
    {
        std::ifstream in(outPath, std::ios::binary);
        std::ostringstream prev;
        prev << in.rdbuf();
        if (in && prev.str() == content.str()) {
            std::fprintf(json ? stderr : stdout, "%s: unchanged (%zu fingerprint(s))\n", outPath.c_str(), n);
            return 0;
        }
    }
    std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
        return 2;
    }
    out << content.str();
    out.close();
    if (!out) {
        std::fprintf(stderr, "cannot write %s\n", outPath.c_str());
        return 2;
    }
    std::fprintf(json ? stderr : stdout, "wrote %s (%zu fingerprint(s))\n", outPath.c_str(), n);
    return 0;
}

int cmdRun(const std::string& path, const std::vector<std::pair<std::string, std::string>>& params,
           const std::vector<std::string>& outputs, const std::string& objDir, unsigned threads,
           const std::vector<std::string>& libRoots, const std::vector<std::string>& probes,
           bool debug, bool fingerprint, bool json, bool profile, const pgg::EvalSpec* eval,
           bool updateGoldens, bool objSplitGroups, bool objColorCheck) {
    pgg::RunParams rp;
    for (const auto& [k, v] : params) rp.values.push_back({k, parseCliValue(v)});
    rp.threads = threads;
    rp.importRoots = libRoots;
    rp.probes = probes;
    rp.debug = debug;
    rp.profile = profile;
    if (eval) rp.evals.push_back(*eval);
    pgg::RunResult result = pgg::runFile(path, rp, outputs);

    // C3: a failed run never rewrites the golden (a broken graph must not
    // bless a partial result).
    if (updateGoldens && !result.hasErrors()) {
        const int rc = writeGoldenFile(path, params, result, json);
        if (rc != 0) return rc;
    }

    if (json) {
        // One JSON document on stdout; diagnostics are in the document (not on
        // stderr), the human report is suppressed. --obj export notes still go
        // to stdout after the document (export stays a human operation).
        nlohmann::ordered_json doc;
        doc["file"] = path;
        doc["ok"] = !result.hasErrors();
        doc["diagnostics"] = diagnosticsToJson(result.diagnostics);
        nlohmann::ordered_json outs = nlohmann::ordered_json::array();
        for (const pgg::RunOutput& o : result.outputs) {
            nlohmann::ordered_json jo;
            jo["name"] = o.name;
            jo["kind"] = valueKindName(o.value);
            jo["summary"] = outputSummaryText(o.value);
            std::optional<uint64_t> fp = fingerprint ? outputFingerprint(o.value) : std::nullopt;
            jo["fingerprint"] = fp ? nlohmann::ordered_json(hex16(*fp)) : nlohmann::ordered_json();
            outs.push_back(std::move(jo));
        }
        doc["outputs"] = std::move(outs);
        nlohmann::ordered_json prs = nlohmann::ordered_json::array();
        for (const pgg::ProbeRecord& pr : result.probes) {
            nlohmann::ordered_json jp;
            jp["origin"] = pr.origin;
            jp["path"] = pr.path;
            jp["inspector"] = pr.inspector;
            jp["text"] = pr.text;
            prs.push_back(std::move(jp));
        }
        doc["probes"] = std::move(prs);
        doc["stats"] = statsToJson(result.stats);
        std::printf("%s\n", doc.dump().c_str());
        return result.hasErrors() ? 1 : 0;
    }

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
            std::printf("%s: %s\n", o.name.c_str(), outputSummaryText(o.value).c_str());
        }
    }
    if (fingerprint) {
        // One line per output, in output order; this block is exactly the
        // baseline file format consumed by diff --baseline. Payloads without a
        // structural hash (sdf / compiled fields) get the "-" placeholder.
        for (const pgg::RunOutput& o : result.outputs) {
            std::optional<uint64_t> fp = outputFingerprint(o.value);
            if (fp)
                std::printf("fingerprint %s: %s\n", o.name.c_str(), hex16(*fp).c_str());
            else
                std::printf("fingerprint %s: - (no structural fingerprint)\n", o.name.c_str());
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
    if (profile) printProfileTable(result.stats);
    if (!objDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(objDir, ec);
        pgg::ObjExportOptions objOpts;
        objOpts.checkColors = objColorCheck;
        for (const pgg::RunOutput& o : result.outputs) {
            if (pgg::valueBase(o.value) == pgg::ScalarType::Sdf) {
                std::printf("skipping %s: sdf outputs are not exported; mesh them with mesh_from_sdf\n",
                            o.name.c_str());
                continue;
            }
            if (pgg::valueBase(o.value) != pgg::ScalarType::Geo) continue;
            // Instances have no polygons of their own: the export realizes
            // them first (the only place instances get expensive, §8.8).
            const pgg::GeoPtr realized =
                pgg::asGeo(o.value)->kind == pgg::GeoKind::Instances
                    ? pgg::realizeInstances(*pgg::asGeo(o.value))
                    : nullptr;
            const pgg::Geo& geo = realized ? *realized : *pgg::asGeo(o.value);
            const char* note = realized ? " (realized from geo<instances>)" : "";
            if (objSplitGroups) {
                // C4: one file per faces-group (house.roof.obj, ...), faces
                // without a group go to <name>._nogroup.obj; a group-less
                // geometry writes the plain <name>.obj.
                std::vector<std::string> written;
                std::string err;
                if (!pgg::writeObjSplitGroups(objDir, o.name, geo, written, &err, objOpts)) {
                    std::fprintf(stderr, "%s\n", err.c_str());
                    return 2;
                }
                for (const std::string& writtenPath : written)
                    std::printf("wrote %s%s\n", writtenPath.c_str(), note);
                continue;
            }
            const std::string objPath = objDir + "/" + o.name + ".obj";
            if (!pgg::writeObj(objPath, geo, nullptr, objOpts)) {
                std::fprintf(stderr, "cannot write %s\n", objPath.c_str());
                return 2;
            }
            std::printf("wrote %s%s\n", objPath.c_str(), note);
        }
    }
    return 0;
}
// --- diff (agent_tooling_plan C2) --------------------------------------------
// Compares two runs output by output (or one run against a saved
// run --fingerprint report). Equal structural fingerprints take the fast path
// to "identical"; geo outputs that differ go through pgg::diffGeo for the
// kind/counts/bbox/+-attr/+-group table and the ΔP stats. sdf/compiled-field
// outputs have no structural fingerprint: they are reported as skipped and do
// not affect the verdict. Exit 0 = all compared outputs identical, 1 =
// differences or run diagnostics with errors, 2 = usage/io.

struct DiffEntry {
    std::string name;
    // identical | different | changed | only_in_a | only_in_b |
    // missing_in_baseline | missing_in_run | skipped
    std::string status;
    std::optional<uint64_t> fpA;
    std::optional<uint64_t> fpB;  // the b run's, or the baseline's recorded one
    pgg::GeoDiffResult geoDiff;
    bool hasGeoDiff = false;
    std::string note;  // scalar-change / kind-change text, or the skip reason
};

// Parses a saved run --fingerprint report: the fingerprint lines
// (`fingerprint <name>: <hex16>` or `fingerprint <name>: - (no structural
// fingerprint)`) are picked out of the full human report — every other line
// (summaries, run/profile stats) is skipped, so `--fingerprint > a.fp` works
// as a baseline as-is. Output order is kept. false + err on io failure or a
// malformed fingerprint line (the caller maps it to exit 2).
bool readBaselineFile(const std::string& path,
                      std::vector<std::pair<std::string, std::optional<uint64_t>>>& out,
                      std::string& err) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        err = "cannot read " + path;
        return false;
    }
    const std::string prefix = "fingerprint ";
    const std::string noFp = "- (no structural fingerprint)";
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        lineNo += 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind(prefix, 0) != 0) continue;  // report prose: skipped
        const std::string rest = line.substr(prefix.size());
        const size_t colon = rest.rfind(':');
        if (colon == std::string::npos) {
            err = path + ":" + std::to_string(lineNo) + ": missing ':'";
            return false;
        }
        std::string name = rest.substr(0, colon);
        std::string value = rest.substr(colon + 1);
        const size_t first = value.find_first_not_of(' ');
        value = first == std::string::npos ? "" : value.substr(first);
        std::optional<uint64_t> fp;
        if (value == noFp) {
            fp = std::nullopt;
        } else {
            char* end = nullptr;
            const unsigned long long v = std::strtoull(value.c_str(), &end, 16);
            if (end == value.c_str() || *end != '\0') {
                err = path + ":" + std::to_string(lineNo) + ": bad hex fingerprint";
                return false;
            }
            fp = static_cast<uint64_t>(v);
        }
        out.push_back({std::move(name), fp});
    }
    return true;
}

nlohmann::ordered_json vec3ToJson(const glm::vec3& v) {
    nlohmann::ordered_json j = nlohmann::ordered_json::array();
    j.push_back(v.x);
    j.push_back(v.y);
    j.push_back(v.z);
    return j;
}

nlohmann::ordered_json geoDiffToJson(const pgg::GeoDiffResult& d) {
    nlohmann::ordered_json j;
    j["kind_a"] = pgg::geoKindName(d.kindA);
    j["kind_b"] = pgg::geoKindName(d.kindB);
    j["points_a"] = d.pointsA;
    j["points_b"] = d.pointsB;
    j["faces_a"] = d.facesA;
    j["faces_b"] = d.facesB;
    j["bbox_a"] = d.hasBBoxA ? nlohmann::ordered_json({vec3ToJson(d.bboxMinA), vec3ToJson(d.bboxMaxA)})
                             : nlohmann::ordered_json();
    j["bbox_b"] = d.hasBBoxB ? nlohmann::ordered_json({vec3ToJson(d.bboxMinB), vec3ToJson(d.bboxMaxB)})
                             : nlohmann::ordered_json();
    nlohmann::ordered_json attrs = nlohmann::ordered_json::array();
    for (const pgg::GeoAttrDelta& a : d.attrs) {
        nlohmann::ordered_json ja;
        ja["name"] = a.name;
        ja["domain"] = pgg::domainName(a.domain);
        ja["type"] = a.type;
        ja["added"] = a.addedInB;
        attrs.push_back(std::move(ja));
    }
    j["attrs"] = std::move(attrs);
    nlohmann::ordered_json groups = nlohmann::ordered_json::array();
    for (const pgg::GeoGroupDelta& g : d.groups) {
        nlohmann::ordered_json jg;
        jg["name"] = g.name;
        jg["domain"] = pgg::domainName(g.domain);
        jg["added"] = g.addedInB;
        groups.push_back(std::move(jg));
    }
    j["groups"] = std::move(groups);
    if (d.hasDeltaP) {
        nlohmann::ordered_json jp;
        jp["max"] = d.deltaPMax;
        jp["mean"] = d.deltaPMean;
        jp["max_index"] = d.deltaPMaxIndex;
        jp["max_group"] = d.deltaPMaxGroup;
        j["delta_p"] = std::move(jp);
    } else {
        j["delta_p"] = nullptr;
    }
    return j;
}

int cmdDiff(const std::string& pathA, const std::string& pathB, const std::string& baselinePath,
            const std::vector<std::string>& outputs, const std::vector<std::string>& libRoots,
            bool json) {
    const bool baselineMode = !baselinePath.empty();
    pgg::RunParams rp;
    rp.importRoots = libRoots;
    pgg::RunResult ra = pgg::runFile(pathA, rp, outputs);
    pgg::RunResult rb;
    if (!baselineMode) rb = pgg::runFile(pathB, rp, outputs);

    std::vector<pgg::Diagnostic> emptyDiags;
    const std::vector<pgg::Diagnostic>& diagsB = baselineMode ? emptyDiags : rb.diagnostics;
    if (ra.hasErrors() || (!baselineMode && rb.hasErrors())) {
        if (json) {
            nlohmann::ordered_json doc;
            doc["a"] = pathA;
            doc["b"] = baselineMode ? nlohmann::ordered_json() : nlohmann::ordered_json(pathB);
            doc["baseline"] = baselineMode ? nlohmann::ordered_json(baselinePath) : nlohmann::ordered_json();
            doc["ok"] = false;
            doc["diagnostics_a"] = diagnosticsToJson(ra.diagnostics);
            doc["diagnostics_b"] = diagnosticsToJson(diagsB);
            std::printf("%s\n", doc.dump().c_str());
        } else {
            for (const pgg::Diagnostic& d : ra.diagnostics) {
                std::fputs(pgg::formatDiagnostic(d, pathA).c_str(), stderr);
                std::fputc('\n', stderr);
            }
            for (const pgg::Diagnostic& d : diagsB) {
                std::fputs(pgg::formatDiagnostic(d, pathB).c_str(), stderr);
                std::fputc('\n', stderr);
            }
        }
        return 1;
    }

    std::vector<DiffEntry> entries;
    if (baselineMode) {
        std::vector<std::pair<std::string, std::optional<uint64_t>>> baseline;
        std::string err;
        if (!readBaselineFile(baselinePath, baseline, err)) {
            std::fprintf(stderr, "%s\n", err.c_str());
            return 2;
        }
        std::map<std::string, std::optional<uint64_t>> byName;
        for (const auto& [n, fp] : baseline) byName[n] = fp;
        std::map<std::string, bool> seen;
        for (const pgg::RunOutput& o : ra.outputs) {
            DiffEntry e;
            e.name = o.name;
            e.fpA = outputFingerprint(o.value);
            seen[o.name] = true;
            auto it = byName.find(o.name);
            if (it == byName.end()) {
                e.status = "missing_in_baseline";
            } else if (!e.fpA || !it->second) {
                e.status = "skipped";
                e.note = "no structural fingerprint";
                e.fpB = it->second;
            } else {
                e.fpB = it->second;
                e.status = *e.fpA == *e.fpB ? "identical" : "changed";
            }
            entries.push_back(std::move(e));
        }
        for (const auto& [n, fp] : baseline) {
            if (seen.count(n)) continue;
            DiffEntry e;
            e.name = n;
            e.status = "missing_in_run";
            e.fpB = fp;
            entries.push_back(std::move(e));
        }
    } else {
        std::map<std::string, const pgg::Value*> valuesA, valuesB;
        for (const pgg::RunOutput& o : ra.outputs) valuesA[o.name] = &o.value;
        for (const pgg::RunOutput& o : rb.outputs) valuesB[o.name] = &o.value;
        std::vector<std::string> names;
        for (const pgg::RunOutput& o : ra.outputs) names.push_back(o.name);
        for (const pgg::RunOutput& o : rb.outputs)
            if (!valuesA.count(o.name)) names.push_back(o.name);
        for (const std::string& name : names) {
            DiffEntry e;
            e.name = name;
            const pgg::Value* va = valuesA.count(name) ? valuesA[name] : nullptr;
            const pgg::Value* vb = valuesB.count(name) ? valuesB[name] : nullptr;
            if (!va) {
                e.status = "only_in_b";
                e.fpB = outputFingerprint(*vb);
            } else if (!vb) {
                e.status = "only_in_a";
                e.fpA = outputFingerprint(*va);
            } else {
                e.fpA = outputFingerprint(*va);
                e.fpB = outputFingerprint(*vb);
                if (e.fpA && e.fpB && *e.fpA == *e.fpB) {
                    e.status = "identical";
                } else if (!e.fpA || !e.fpB) {
                    e.status = "skipped";
                    e.note = "no structural fingerprint";
                } else if (pgg::valueBase(*va) == pgg::ScalarType::Geo &&
                           pgg::valueBase(*vb) == pgg::ScalarType::Geo) {
                    e.status = "different";
                    e.geoDiff = pgg::diffGeo(*pgg::asGeo(*va), *pgg::asGeo(*vb));
                    e.hasGeoDiff = true;
                } else {
                    e.status = "different";
                    const std::string kindA = valueKindName(*va);
                    const std::string kindB = valueKindName(*vb);
                    if (kindA != kindB) {
                        e.note = kindA + " \xe2\x86\x92 " + kindB;
                    } else {
                        e.note = kindA + " " + pgg::valueToString(*va) + " \xe2\x86\x92 " +
                                 pgg::valueToString(*vb);
                    }
                }
            }
            entries.push_back(std::move(e));
        }
    }

    int nIdentical = 0, nDifferent = 0, nOnlyA = 0, nOnlyB = 0, nSkipped = 0;
    for (const DiffEntry& e : entries) {
        if (e.status == "identical") nIdentical += 1;
        else if (e.status == "different" || e.status == "changed") nDifferent += 1;
        else if (e.status == "only_in_a" || e.status == "missing_in_baseline") nOnlyA += 1;
        else if (e.status == "only_in_b" || e.status == "missing_in_run") nOnlyB += 1;
        else nSkipped += 1;
    }
    const bool identical = nDifferent == 0 && nOnlyA == 0 && nOnlyB == 0;

    if (json) {
        nlohmann::ordered_json doc;
        doc["a"] = pathA;
        doc["b"] = baselineMode ? nlohmann::ordered_json() : nlohmann::ordered_json(pathB);
        doc["baseline"] = baselineMode ? nlohmann::ordered_json(baselinePath) : nlohmann::ordered_json();
        doc["ok"] = true;
        doc["diagnostics_a"] = diagnosticsToJson(ra.diagnostics);
        doc["diagnostics_b"] = diagnosticsToJson(diagsB);
        nlohmann::ordered_json outs = nlohmann::ordered_json::array();
        for (const DiffEntry& e : entries) {
            nlohmann::ordered_json jo;
            jo["name"] = e.name;
            jo["status"] = e.status;
            jo["fingerprint_a"] =
                e.fpA ? nlohmann::ordered_json(hex16(*e.fpA)) : nlohmann::ordered_json();
            jo["fingerprint_b"] =
                e.fpB ? nlohmann::ordered_json(hex16(*e.fpB)) : nlohmann::ordered_json();
            if (!e.note.empty()) jo["note"] = e.note;
            if (e.hasGeoDiff)
                jo["diff"] = geoDiffToJson(e.geoDiff);
            else
                jo["diff"] = nullptr;
            outs.push_back(std::move(jo));
        }
        doc["outputs"] = std::move(outs);
        doc["stats_a"] = statsToJson(ra.stats);
        doc["stats_b"] = baselineMode ? nlohmann::ordered_json() : statsToJson(rb.stats);
        doc["identical"] = identical;
        std::printf("%s\n", doc.dump().c_str());
        return identical ? 0 : 1;
    }

    const char* onlyALabel = baselineMode ? "missing in baseline" : "only in a";
    const char* onlyBLabel = baselineMode ? "missing in run" : "only in b";
    for (const DiffEntry& e : entries) {
        if (e.status == "identical") {
            std::printf("%s: identical\n", e.name.c_str());
        } else if (e.hasGeoDiff) {
            const std::vector<std::string> lines = pgg::formatGeoDiff(e.geoDiff);
            for (size_t i = 0; i < lines.size(); ++i) {
                if (i == 0)
                    std::printf("%s: %s\n", e.name.c_str(), lines[i].c_str());
                else
                    std::printf("  %s\n", lines[i].c_str());
            }
        } else if (e.status == "different") {
            std::printf("%s: %s\n", e.name.c_str(), e.note.c_str());
        } else if (e.status == "changed") {
            std::printf("%s: changed (baseline %s, run %s)\n", e.name.c_str(),
                        e.fpB ? hex16(*e.fpB).c_str() : "-", e.fpA ? hex16(*e.fpA).c_str() : "-");
        } else if (e.status == "skipped") {
            std::printf("%s: skipped (%s)\n", e.name.c_str(), e.note.c_str());
        } else {
            std::printf("%s: %s\n", e.name.c_str(),
                        e.status == "only_in_a" || e.status == "missing_in_baseline" ? onlyALabel
                                                                                     : onlyBLabel);
        }
    }
    if (identical) {
        std::printf("diff: identical\n");
    } else {
        std::string tail;
        char buf[128];
        std::snprintf(buf, sizeof(buf), "%d compared, %d identical, %d different",
                      nIdentical + nDifferent, nIdentical, nDifferent);
        tail += buf;
        if (nOnlyA) {
            std::snprintf(buf, sizeof(buf), ", %d %s", nOnlyA, onlyALabel);
            tail += buf;
        }
        if (nOnlyB) {
            std::snprintf(buf, sizeof(buf), ", %d %s", nOnlyB, onlyBLabel);
            tail += buf;
        }
        if (nSkipped) {
            std::snprintf(buf, sizeof(buf), ", %d skipped", nSkipped);
            tail += buf;
        }
        std::printf("diff: different (%s)\n", tail.c_str());
    }
    return identical ? 0 : 1;
}

// --- docs builtin / docs builtins (agent_tooling_plan D3) -------------------
// The builtin catalog from the live registry (signature) + builtin_docs
// (summary/example). No file needed — the registry is file-independent.

int cmdDocsBuiltin(const std::string& name) {
    const pgg::BuiltinDoc* doc = pgg::findBuiltinDoc(name);
    const pgg::BuiltinSig* sig = pgg::findBuiltin(name);
    if (!doc || !sig) {
        // Nearest by prefix (both directions), then by substring; sorted.
        std::vector<std::string> near;
        for (const pgg::BuiltinDoc& d : pgg::allBuiltinDocs())
            if (d.name.rfind(name, 0) == 0 || (name.size() >= 3 && name.rfind(d.name, 0) == 0))
                near.push_back(d.name);
        if (near.empty())
            for (const pgg::BuiltinDoc& d : pgg::allBuiltinDocs())
                if (d.name.find(name) != std::string::npos) near.push_back(d.name);
        std::sort(near.begin(), near.end());
        if (near.size() > 5) near.resize(5);
        std::fprintf(stderr, "unknown builtin '%s'", name.c_str());
        if (!near.empty()) {
            std::fprintf(stderr, "; did you mean:");
            for (const std::string& n : near) std::fprintf(stderr, " %s", n.c_str());
        }
        std::fprintf(stderr, "\n");
        return 1;
    }
    std::puts(pgg::builtinSignatureText(*sig).c_str());
    std::printf("group: %s\n\n", doc->group.c_str());
    if (sig->deferredStage) std::printf("NOT SUPPORTED at this stage: %s\n\n", sig->deferredStage);
    std::puts(doc->summary.c_str());
    std::printf("\nExample:\n");
    printIndented(doc->example);
    return 0;
}

int cmdDocsBuiltins(const std::string& group) {
    // Valid groups in first-appearance order, for the unknown-group error.
    std::vector<std::string> groups;
    for (const pgg::BuiltinDoc& d : pgg::allBuiltinDocs())
        if (std::find(groups.begin(), groups.end(), d.group) == groups.end()) groups.push_back(d.group);
    if (!group.empty() && std::find(groups.begin(), groups.end(), group) == groups.end()) {
        std::fprintf(stderr, "unknown group '%s'; groups:", group.c_str());
        for (const std::string& g : groups) std::fprintf(stderr, " %s", g.c_str());
        std::fprintf(stderr, "\n");
        return 1;
    }
    size_t shown = 0;
    for (const pgg::BuiltinSig& s : pgg::builtinRegistry()) {
        const pgg::BuiltinDoc* doc = pgg::findBuiltinDoc(s.name);
        if (!doc) continue;  // completeness is pinned by builtin_docs_test
        if (!group.empty() && doc->group != group) continue;
        std::puts(pgg::builtinSignatureText(s).c_str());
        shown += 1;
    }
    std::printf("%zu builtin(s)%s%s\n", shown, group.empty() ? "" : " in group ", group.c_str());
    return 0;
}


// The lookup itself lives in pgg lib (eval/docs_lookup.h), shared with the
// viewer RPC docs command.

// --- docs (spec §7.5): a def's signature as written + its docstring ---------
// The lookup itself lives in pgg lib (eval/docs_lookup.h), shared with the
// viewer RPC docs command.

int cmdDocs(const std::string& path, const std::string& symbol, const std::vector<std::string>& libRoots) {
    pgg::Document doc = pgg::parseFile(path);
    for (const pgg::Diagnostic& d : doc.diagnostics) {
        if (!d.isWarning) {
            std::fputs(pgg::formatDiagnostic(d, path).c_str(), stderr);
            std::fputc('\n', stderr);
        }
    }
    if (!doc.file) return 1;

    pgg::DocsLookupResult res = pgg::findDef(*doc.file, path, symbol, libRoots);
    for (const pgg::Diagnostic& d : res.diagnostics) {
        std::fputs(pgg::formatDiagnostic(d, path).c_str(), stderr);
        std::fputc('\n', stderr);
    }
    if (!res.found) {
        if (!res.error.empty()) std::fprintf(stderr, "%s\n", res.error.c_str());
        return 1;
    }
    std::puts(res.signature.c_str());
    if (res.hasDoc) {
        std::puts(res.docstring.c_str());
    } else {
        std::puts("(no docstring)");
    }
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
    if (cmd == "diff") {
        std::vector<std::string> files;
        std::vector<std::string> outputs;
        std::vector<std::string> libRoots;
        std::string baseline;
        bool json = false;
        for (int i = 2; i < argc; ++i) {
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
            if (takeValue("--output", v)) {
                outputs.push_back(v);
            } else if (takeValue("--lib", v)) {
                libRoots.push_back(v);
            } else if (takeValue("--baseline", v)) {
                baseline = v;
            } else if (a == "--json") {
                json = true;
            } else if (!a.empty() && a[0] == '-') {
                usage();
                return 2;
            } else {
                files.push_back(a);
            }
        }
        // Two-file mode needs a.pgg + b.pgg; --baseline replaces b.pgg.
        if (baseline.empty() ? files.size() != 2 : files.size() != 1) {
            usage();
            return 2;
        }
        return cmdDiff(files[0], files.size() > 1 ? files[1] : "", baseline, outputs, libRoots,
                       json);
    }
    if (cmd == "docs") {
        // docs <file.pgg> <symbol> [--lib <dir>]...  — a def's card (§7.5)
        // docs builtin <name>                        — a builtin's card (D3)
        // docs builtins [--group <g>]                — the builtin catalog (D3)
        if (path == "builtin") {
            if (argc < 4) {
                usage();
                return 2;
            }
            return cmdDocsBuiltin(argv[3]);
        }
        if (path == "builtins") {
            std::string group;
            for (int i = 3; i < argc; ++i) {
                const std::string a = argv[i];
                if (a == "--group" && i + 1 < argc) {
                    group = argv[++i];
                } else if (a.rfind("--group=", 0) == 0) {
                    group = a.substr(8);
                } else {
                    usage();
                    return 2;
                }
            }
            return cmdDocsBuiltins(group);
        }
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
        bool fingerprint = false;
        bool json = false;
        bool profile = false;
        bool objSplitGroups = false;
        std::string objColor;
        bool updateGoldens = false;
        std::string evalExpr, evalOn;
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
            } else if (takeValue("--eval", v)) {
                evalExpr = v;
            } else if (takeValue("--on", v)) {
                evalOn = v;
            } else if (takeValue("--obj-color", v)) {
                objColor = v;
            } else if (a == "--obj-split-groups") {
                objSplitGroups = true;
            } else if (a == "--update-goldens") {
                updateGoldens = true;
            } else if (a == "--debug") {
                debug = true;
            } else if (a == "--fingerprint") {
                fingerprint = true;
            } else if (a == "--profile") {
                profile = true;
            } else if (a == "--json") {
                json = true;
            } else {
                usage();
                return 2;
            }
        }
        // B6: --eval and --on are a pair; each alone is a usage error.
        if (evalExpr.empty() != evalOn.empty()) {
            std::fprintf(stderr, "--eval and --on must be given together\n");
            usage();
            return 2;
        }
        // C4: the only color mode so far is the check classification.
        if (!objColor.empty() && objColor != "check") {
            std::fprintf(stderr, "unknown --obj-color '%s' (want: check)\n", objColor.c_str());
            return 2;
        }
        if ((objSplitGroups || !objColor.empty()) && objDir.empty()) {
            std::fprintf(stderr, "--obj-split-groups/--obj-color need --obj <dir>\n");
            usage();
            return 2;
        }
        pgg::EvalSpec evalSpec{evalExpr, evalOn};
        return cmdRun(path, params, outputs, objDir, threads, libRoots, probes, debug, fingerprint,
                      json, profile, evalExpr.empty() ? nullptr : &evalSpec, updateGoldens,
                      objSplitGroups, objColor == "check");
    }
    if (cmd == "check") {
        // `check <file.pgg> [--json]` or `check --explain <code>` (D2: the
        // code reference card, no file needed).
        bool json = false;
        std::string explain;
        std::string file;
        for (int i = 2; i < argc; ++i) {
            const std::string a = argv[i];
            if (a == "--json") {
                json = true;
            } else if (a == "--explain" && i + 1 < argc) {
                explain = argv[++i];
            } else if (a.rfind("--explain=", 0) == 0) {
                explain = a.substr(10);
            } else if (!a.empty() && a[0] == '-') {
                usage();
                return 2;
            } else {
                file = a;
            }
        }
        if (!explain.empty()) return cmdExplain(explain);
        if (file.empty()) {
            usage();
            return 2;
        }
        return cmdCheck(file, json);
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
    if (cmd == "ast") return cmdAst(path);
    if (cmd == "fmt") return cmdFmt(path, inPlace, checkOnly);
    usage();
    return 2;
}
