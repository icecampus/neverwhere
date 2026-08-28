// PggTool: CLI front-end for the PGG language (spec E0+E1).
//   PggTool check <file.pgg> [--json]   parse + lint, print diagnostics
//   PggTool fmt <file.pgg> [--check]    canonical format
//   PggTool ast <file.pgg>              dump the AST
//   PggTool run <file.pgg> [--param k=v]... [--output name]... [--obj <dir>]
//                                       run the graph, print output summaries
// Exit codes: 0 ok, 1 diagnostics with errors, 2 usage/io failure.

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include <pgg/eval.h>
#include <pgg/pgg.h>

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  PggTool check <file.pgg> [--json]   parse + lint, print diagnostics\n"
                 "  PggTool fmt <file.pgg> [-i|--check] canonical format (stdout, write-back, or diff-check)\n"
                 "  PggTool ast <file.pgg>              dump the AST\n"
                 "  PggTool run <file.pgg> [--param k=v]... [--output name]... [--obj <dir>]\n"
                 "                                      run the graph, print output summaries\n"
                 "                                      (--obj writes one Wavefront OBJ per geo output)\n");
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
    if (geo.pointCount() > 0) {
        glm::vec3 mn, mx;
        pgg::geoBBox(geo, mn, mx);
        std::printf(" bbox=(%g, %g, %g)..(%g, %g, %g)", mn.x, mn.y, mn.z, mx.x, mx.y, mx.z);
    }
    std::printf("\n");
}

bool writeObj(const std::string& path, const pgg::Geo& geo) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "# PggTool run export\n";
    for (const glm::vec3& p : *geo.positions)
        out << "v " << p.x << " " << p.y << " " << p.z << "\n";
    if (geo.kind == pgg::GeoKind::Mesh) {
        // Fan triangulation of polygon faces, 1-based indices.
        for (size_t f = 0; f < geo.faceCount(); ++f) {
            const int32_t begin = (*geo.faceOffsets)[f];
            const int32_t end = (*geo.faceOffsets)[f + 1];
            for (int32_t c = begin + 1; c + 1 < end; ++c) {
                out << "f " << (*geo.cornerVerts)[begin] + 1 << " " << (*geo.cornerVerts)[c] + 1 << " "
                    << (*geo.cornerVerts)[c + 1] + 1 << "\n";
            }
        }
    }
    return static_cast<bool>(out);
}

int cmdRun(const std::string& path, const std::vector<std::pair<std::string, std::string>>& params,
           const std::vector<std::string>& outputs, const std::string& objDir) {
    pgg::RunParams rp;
    for (const auto& [k, v] : params) rp.values.push_back({k, parseCliValue(v)});
    pgg::RunResult result = pgg::runFile(path, rp, outputs);
    for (const pgg::Diagnostic& d : result.diagnostics) {
        std::fputs(pgg::formatDiagnostic(d, path).c_str(), stderr);
        std::fputc('\n', stderr);
    }
    if (result.hasErrors()) return 1;
    for (const pgg::RunOutput& o : result.outputs) {
        if (pgg::valueBase(o.value) == pgg::ScalarType::Geo) {
            printGeoSummary(o.name, *pgg::asGeo(o.value));
        } else {
            std::printf("%s: %s = %s\n", o.name.c_str(), pgg::scalarName(pgg::valueBase(o.value)),
                        pgg::valueToString(o.value).c_str());
        }
    }
    std::printf("run: %zu output(s), %llu field evaluation(s)\n", result.outputs.size(),
                (unsigned long long)result.stats.fieldsEvaluated);
    if (!objDir.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(objDir, ec);
        for (const pgg::RunOutput& o : result.outputs) {
            if (pgg::valueBase(o.value) != pgg::ScalarType::Geo) continue;
            const std::string objPath = objDir + "/" + o.name + ".obj";
            if (!writeObj(objPath, *pgg::asGeo(o.value))) {
                std::fprintf(stderr, "cannot write %s\n", objPath.c_str());
                return 2;
            }
            std::printf("wrote %s\n", objPath.c_str());
        }
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
    if (cmd == "run") {
        std::vector<std::pair<std::string, std::string>> params;
        std::vector<std::string> outputs;
        std::string objDir;
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
            } else {
                usage();
                return 2;
            }
        }
        return cmdRun(path, params, outputs, objDir);
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
