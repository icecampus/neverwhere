#pragma once

// Golden fingerprint files (agent_tooling_plan C3): src/tests/pgg/goldens/
// <name>.fp stores one `fingerprint <output>: <hex16>` line per run output —
// the same format `PggTool run --fingerprint` prints and `diff --baseline`
// reads — recorded by `PggTool run <file> --update-goldens` (from the repo
// root). A test compares the structural fingerprint (pgg::fingerprintValue)
// of every run output against the recorded one instead of pinning literal
// counts/bbox values in the source.
//
// Rules: sdf / compiled-field outputs carry `- (no structural fingerprint)`
// and are skipped (as in diff); a fingerprintable output missing from the
// golden, a stale golden entry missing from the run, and any hex mismatch all
// FAIL with the actual hex values and the re-record hint. Comment lines (`#`)
// and any other non-fingerprint lines are ignored by the parser.

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>

#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/src/eval/fingerprint.h"

namespace pggtest {

inline std::string goldenPath(const std::string& name) {
    return std::string(PGG_GOLDENS_DIR) + "/" + name + ".fp";
}

// The recorded fingerprints of one golden file: output name -> hex (nullopt =
// the `- (no structural fingerprint)` placeholder). False + err on io/format
// failure; non-fingerprint lines are skipped.
inline bool loadGoldenFingerprints(
    const std::string& path, std::vector<std::pair<std::string, std::optional<uint64_t>>>& out,
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
        if (line.rfind(prefix, 0) != 0) continue;  // comments / prose: skipped
        const std::string rest = line.substr(prefix.size());
        const size_t colon = rest.rfind(':');
        if (colon == std::string::npos) {
            err = path + ":" + std::to_string(lineNo) + ": missing ':'";
            return false;
        }
        std::string value = rest.substr(colon + 1);
        const size_t first = value.find_first_not_of(' ');
        value = first == std::string::npos ? "" : value.substr(first);
        std::optional<uint64_t> fp;
        if (value != noFp) {
            char* end = nullptr;
            const unsigned long long v = std::strtoull(value.c_str(), &end, 16);
            if (end == value.c_str() || *end != '\0') {
                err = path + ":" + std::to_string(lineNo) + ": bad hex fingerprint";
                return false;
            }
            fp = static_cast<uint64_t>(v);
        }
        out.push_back({rest.substr(0, colon), fp});
    }
    return true;
}

inline std::string goldenHex(const std::optional<uint64_t>& fp) {
    if (!fp) return "- (no structural fingerprint)";
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(*fp));
    return buf;
}

// Compares every output of `r` against the golden file goldens/<name>.fp.
inline void expectGolden(const std::string& name, const pgg::RunResult& r) {
    const std::string path = goldenPath(name);
    std::vector<std::pair<std::string, std::optional<uint64_t>>> golden;
    std::string err;
    if (!loadGoldenFingerprints(path, golden, err)) {
        ADD_FAILURE() << err << "\nrecord the golden with: PggTool run <file.pgg> --update-goldens"
                      << " (from the repo root; writes src/tests/pgg/goldens/<basename>.fp)";
        return;
    }
    std::map<std::string, std::optional<uint64_t>> byName;
    for (const auto& [n, fp] : golden) byName[n] = fp;
    std::map<std::string, bool> seen;
    bool ok = true;
    std::ostringstream report;
    for (const pgg::RunOutput& o : r.outputs) {
        uint64_t fpRaw = 0;
        const bool hasFp = pgg::fingerprintValue(o.value, fpRaw);
        seen[o.name] = true;
        if (!hasFp) continue;  // sdf / compiled field: no structural fingerprint (as in diff)
        const auto it = byName.find(o.name);
        if (it == byName.end()) {
            ok = false;
            report << "\n  " << o.name << ": missing in the golden (actual "
                   << goldenHex(std::optional<uint64_t>(fpRaw)) << ")";
            continue;
        }
        if (!it->second) continue;  // golden records the no-fingerprint placeholder
        if (*it->second != fpRaw) {
            ok = false;
            report << "\n  " << o.name << ": golden " << goldenHex(it->second) << ", actual "
                   << goldenHex(std::optional<uint64_t>(fpRaw));
        }
    }
    for (const auto& [n, fp] : golden) {
        if (seen.count(n) || !fp) continue;
        ok = false;
        report << "\n  " << n << ": stale golden entry, the run has no such output";
    }
    EXPECT_TRUE(ok) << "golden mismatch against " << path << report.str()
                    << "\nif the numeric profile changed on purpose, re-record with: PggTool run"
                    << " <file.pgg> --update-goldens (from the repo root)";
}

}  // namespace pggtest
