// PggTool: CLI front-end for the PGG language (spec E0).
//   PggTool check <file.pgg> [--json]   parse + lint, print diagnostics
//   PggTool fmt <file.pgg> [--check]    canonical format
//   PggTool ast <file.pgg>              dump the AST
// Exit codes: 0 ok, 1 diagnostics with errors, 2 usage/io failure.

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

#include <pgg/pgg.h>

namespace {

void usage() {
    std::fprintf(stderr,
                 "usage:\n"
                 "  PggTool check <file.pgg> [--json]   parse + lint, print diagnostics\n"
                 "  PggTool fmt <file.pgg> [-i|--check] canonical format (stdout, write-back, or diff-check)\n"
                 "  PggTool ast <file.pgg>              dump the AST\n");
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

int cmdFmt(const std::string& path, bool inPlace, bool checkOnly) {
    pgg::Document doc = pgg::parseFile(path);
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

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) {
        usage();
        return 2;
    }
    const std::string cmd = argv[1];
    const std::string path = argv[2];
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
