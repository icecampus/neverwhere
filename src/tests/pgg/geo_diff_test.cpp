// Geo diff tests (agent_tooling_plan C2): pgg::diffGeo over inline PGG runs —
// identical runs (fingerprint fast-path input), count changes, the ΔP stats
// with a faces group for the max point, and +/- attr/group deltas.
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/src/eval/fingerprint.h"
#include "pgg/src/eval/geo_diff.h"
#include "test_utils.h"

namespace {

TEST(GeoDiff, IdenticalRunsHaveEqualFingerprintsAndEmptyDiff) {
    const char* src =
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = mark(b, \"top\", where = dot(@N, (0, 1, 0)) > 0.8)\n"
        "output m\n";
    pgg::RunResult r1 = pgg::run(src);
    pgg::RunResult r2 = pgg::run(src);
    pggtest::expectNoErrors(r1);
    pggtest::expectNoErrors(r2);
    const pgg::Value* v1 = pggtest::outputOf(r1, "m");
    const pgg::Value* v2 = pggtest::outputOf(r2, "m");
    ASSERT_TRUE(v1);
    ASSERT_TRUE(v2);
    uint64_t fp1 = 0, fp2 = 0;
    ASSERT_TRUE(pgg::fingerprintValue(*v1, fp1));
    ASSERT_TRUE(pgg::fingerprintValue(*v2, fp2));
    EXPECT_EQ(fp1, fp2);

    pgg::GeoDiffResult d = pgg::diffGeo(*pgg::asGeo(*v1), *pgg::asGeo(*v2));
    EXPECT_EQ(d.kindA, d.kindB);
    EXPECT_EQ(d.pointsA, d.pointsB);
    EXPECT_EQ(d.facesA, d.facesB);
    EXPECT_TRUE(d.attrs.empty());
    EXPECT_TRUE(d.groups.empty());
    ASSERT_TRUE(d.hasDeltaP);
    EXPECT_FLOAT_EQ(d.deltaPMax, 0.0f);
    EXPECT_FLOAT_EQ(d.deltaPMean, 0.0f);
}

TEST(GeoDiff, DifferentCountsReportCountsAndSkipDeltaP) {
    pgg::RunResult ra = pgg::run("b = ico_sphere(subdiv = 1, radius = 1.0)\noutput b\n");
    pgg::RunResult rb = pgg::run("b = ico_sphere(subdiv = 2, radius = 1.0)\noutput b\n");
    pggtest::expectNoErrors(ra);
    pggtest::expectNoErrors(rb);
    pgg::GeoPtr ga = pggtest::geoOutput(ra, "b");
    pgg::GeoPtr gb = pggtest::geoOutput(rb, "b");
    ASSERT_TRUE(ga);
    ASSERT_TRUE(gb);

    pgg::GeoDiffResult d = pgg::diffGeo(*ga, *gb);
    EXPECT_NE(d.pointsA, d.pointsB);
    EXPECT_NE(d.facesA, d.facesB);
    EXPECT_FALSE(d.hasDeltaP);

    const std::vector<std::string> lines = pgg::formatGeoDiff(d);
    ASSERT_FALSE(lines.empty());
    EXPECT_NE(lines[0].find("kind mesh"), std::string::npos) << lines[0];
    EXPECT_NE(lines[0].find("bbox"), std::string::npos) << lines[0];
    // No ΔP lines when the point counts differ.
    for (const std::string& line : lines) EXPECT_EQ(line.find("\xce\x94P"), std::string::npos) << line;
}

TEST(GeoDiff, DeltaPFindsTheMovedPointAndItsFacesGroup) {
    // Same topology on both sides; b moves exactly one point by (1, 0, 0).
    // Faces groups: "all" covers everything, "aaa" is empty (sorts first, must
    // be skipped), "zzz" covers everything (sorts last, "all" wins).
    const std::string base =
        "g = grid(size = (4, 4), res = 2)\n"
        "m = mark(g, \"all\", where = true, domain = faces)\n"
        "m2 = mark(m, \"aaa\", where = false, domain = faces)\n"
        "m3 = mark(m2, \"zzz\", where = true, domain = faces)\n";
    pgg::RunResult ra = pgg::run(base + "output m3\n");
    pgg::RunResult rb = pgg::run(
        base + "s = set_position(m3, offset = (1, 0, 0), where = @index == 5)\noutput s\n");
    pggtest::expectNoErrors(ra);
    pggtest::expectNoErrors(rb);
    pgg::GeoPtr ga = pggtest::geoOutput(ra, "m3");
    pgg::GeoPtr gb = pggtest::geoOutput(rb, "s");
    ASSERT_TRUE(ga);
    ASSERT_TRUE(gb);
    ASSERT_EQ(ga->pointCount(), 9u);  // res 2 -> 3x3 points, 4 quads
    ASSERT_EQ(ga->pointCount(), gb->pointCount());

    pgg::GeoDiffResult d = pgg::diffGeo(*ga, *gb);
    ASSERT_TRUE(d.hasDeltaP);
    EXPECT_FLOAT_EQ(d.deltaPMax, 1.0f);
    EXPECT_EQ(d.deltaPMaxIndex, 5u);
    EXPECT_NEAR(d.deltaPMean, 1.0f / 9.0f, 1e-6f);
    EXPECT_EQ(d.deltaPMaxGroup, "all");

    const std::vector<std::string> lines = pgg::formatGeoDiff(d);
    ASSERT_GE(lines.size(), 3u);  // header + ΔP max + ΔP mean
    EXPECT_NE(lines[1].find("(point #5, group all)"), std::string::npos) << lines[1];
    EXPECT_NE(lines[2].find("mean"), std::string::npos) << lines[2];
}

TEST(GeoDiff, AttrAndGroupDeltas) {
    pgg::RunResult ra = pgg::run(
        "g = grid(size = (4, 4), res = 2)\n"
        "m = mark(g, \"left\", where = dot(@P, (1, 0, 0)) < 0, domain = faces)\n"
        "s = set(m, \"slope\", dot(@N, (0, 0, 1)), domain = points)\n"
        "output s\n");
    pgg::RunResult rb = pgg::run(
        "g = grid(size = (4, 4), res = 2)\n"
        "m = mark(g, \"right\", where = dot(@P, (1, 0, 0)) > 0, domain = faces)\n"
        "s = set(m, \"height\", dot(@P, (0, 0, 1)), domain = points)\n"
        "output s\n");
    pggtest::expectNoErrors(ra);
    pggtest::expectNoErrors(rb);
    pgg::GeoPtr ga = pggtest::geoOutput(ra, "s");
    pgg::GeoPtr gb = pggtest::geoOutput(rb, "s");
    ASSERT_TRUE(ga);
    ASSERT_TRUE(gb);

    pgg::GeoDiffResult d = pgg::diffGeo(*ga, *gb);
    // -attr slope(f32,points), +attr height(f32,points); the grid's @uv is on
    // both sides and must not show up.
    ASSERT_EQ(d.attrs.size(), 2u);
    EXPECT_EQ(d.attrs[0].name, "height");
    EXPECT_EQ(d.attrs[0].domain, pgg::Domain::Points);
    EXPECT_EQ(d.attrs[0].type, "f32");
    EXPECT_TRUE(d.attrs[0].addedInB);
    EXPECT_EQ(d.attrs[1].name, "slope");
    EXPECT_EQ(d.attrs[1].domain, pgg::Domain::Points);
    EXPECT_EQ(d.attrs[1].type, "f32");
    EXPECT_FALSE(d.attrs[1].addedInB);
    // -group left(faces), +group right(faces).
    ASSERT_EQ(d.groups.size(), 2u);
    EXPECT_EQ(d.groups[0].name, "left");
    EXPECT_EQ(d.groups[0].domain, pgg::Domain::Faces);
    EXPECT_FALSE(d.groups[0].addedInB);
    EXPECT_EQ(d.groups[1].name, "right");
    EXPECT_EQ(d.groups[1].domain, pgg::Domain::Faces);
    EXPECT_TRUE(d.groups[1].addedInB);

    // Text contract pins.
    const std::vector<std::string> lines = pgg::formatGeoDiff(d);
    std::string joined;
    for (const std::string& line : lines) joined += line + "\n";
    EXPECT_NE(joined.find("-attr slope(f32,points)"), std::string::npos) << joined;
    EXPECT_NE(joined.find("+attr height(f32,points)"), std::string::npos) << joined;
    EXPECT_NE(joined.find("-group left(faces)"), std::string::npos) << joined;
    EXPECT_NE(joined.find("+group right(faces)"), std::string::npos) << joined;
}

}  // namespace
