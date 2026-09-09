// C3 (docs/pgg/agent_tooling_plan.md): the golden .fp file parser of
// goldens_utils.h — comment/prose lines are skipped, the
// `- (no structural fingerprint)` placeholder maps to nullopt, malformed
// lines are rejected with the line number. (The comparison half,
// expectGolden, is covered by the migrated corpus goldens: tower / e4_sdf_rock
// / e4_mesh_sphere / e7_fracture.)
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "goldens_utils.h"

namespace {

std::filesystem::path goldenScratch(const std::string& name, const std::string& content) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "pgg_goldens_test";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::filesystem::path path = dir / name;
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << content;
    return path;
}

TEST(Goldens, ParserReadsHexAndSkipsProse) {
    const std::filesystem::path path = goldenScratch(
        "ok.fp",
        "# pgg golden, updated by PggTool run --update-goldens\n"
        "# params: world_seed=42\n"
        "fingerprint scene: 0123456789abcdef\n"
        "some report prose line\n"
        "fingerprint rock_field: - (no structural fingerprint)\n"
        "fingerprint anchors: fedcba9876543210\r\n");  // CRLF tolerated
    std::vector<std::pair<std::string, std::optional<uint64_t>>> entries;
    std::string err;
    ASSERT_TRUE(pggtest::loadGoldenFingerprints(path.string(), entries, err)) << err;
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].first, "scene");
    ASSERT_TRUE(entries[0].second.has_value());
    EXPECT_EQ(*entries[0].second, 0x0123456789abcdefull);
    EXPECT_EQ(entries[1].first, "rock_field");
    EXPECT_FALSE(entries[1].second.has_value());
    EXPECT_EQ(entries[2].first, "anchors");
    ASSERT_TRUE(entries[2].second.has_value());
    EXPECT_EQ(*entries[2].second, 0xfedcba9876543210ull);
}

TEST(Goldens, ParserRejectsBadHexAndMissingColon) {
    const std::filesystem::path bad = goldenScratch("bad_hex.fp", "fingerprint a: zz\n");
    std::vector<std::pair<std::string, std::optional<uint64_t>>> entries;
    std::string err;
    EXPECT_FALSE(pggtest::loadGoldenFingerprints(bad.string(), entries, err));
    EXPECT_TRUE(err.find("bad hex fingerprint") != std::string::npos) << err;

    const std::filesystem::path noColon = goldenScratch("no_colon.fp", "fingerprint a\n");
    EXPECT_FALSE(pggtest::loadGoldenFingerprints(noColon.string(), entries, err));
    EXPECT_TRUE(err.find("missing ':'") != std::string::npos) << err;
}

TEST(Goldens, ParserReportsMissingFile) {
    std::vector<std::pair<std::string, std::optional<uint64_t>>> entries;
    std::string err;
    EXPECT_FALSE(pggtest::loadGoldenFingerprints(
        (std::filesystem::temp_directory_path() / "pgg_goldens_test" / "no_such.fp").string(), entries,
        err));
    EXPECT_TRUE(err.find("cannot read") != std::string::npos) << err;
}

}  // namespace
