// E-profile and --eval tests (agent_tooling_plan §5 E, §2 B6): RunParams.profile
// records one exclusive wall-time row per evaluated binding (cache hits flagged,
// in-run memoization free); RunParams.evals compiles an ad-hoc expression in the
// file's context, evaluates it as a field on the points domain of the
// on-geometry, materializes it as @__eval and reports the stats inspector's
// output as an "eval" probe record (outputs are computed as usual).
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/src/eval/cache.h"
#include "test_utils.h"

namespace {

const pgg::BindingProfile* profileRow(const pgg::RunResult& r, const std::string& name) {
    for (const pgg::BindingProfile& b : r.stats.profile)
        if (b.name == name) return &b;
    return nullptr;
}

const pgg::ProbeRecord* evalRecord(const pgg::RunResult& r, const std::string& path) {
    for (const pgg::ProbeRecord& pr : r.probes)
        if (pr.origin == "eval" && pr.path == path) return &pr;
    return nullptr;
}

bool hasMessage(const pgg::RunResult& r, const std::string& code, const std::string& needle) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code && d.message.find(needle) != std::string::npos) return true;
    return false;
}

// ico_sphere subdiv 1: 42 pts, 80 tri, @N present (same fixture as probe tests).
const char* kGeo =
    "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
    "output base\n";

// --- profile -------------------------------------------------------------------

TEST(EvalProfile, OffByDefault) {
    pgg::RunResult r = pgg::run(std::string(kGeo), pgg::RunParams{});
    pggtest::expectNoErrors(r);
    EXPECT_TRUE(r.stats.profile.empty());
}

TEST(EvalProfile, RowsPerEvaluatedBinding) {
    const std::string src =
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = set(base, \"slope\", dot(@N, (0, 1, 0)))\n"
        "output m\n";
    pgg::RunParams p;
    p.profile = true;
    pgg::RunResult r = pgg::run(src, p);
    pggtest::expectNoErrors(r);

    const pgg::BindingProfile* m = profileRow(r, "m");
    ASSERT_TRUE(m);
    EXPECT_FALSE(m->cacheHit);
    EXPECT_GE(m->ms, 0.0);
    EXPECT_GE(m->fieldEvals, 1u);  // the slope field evaluated on 42 points

    const pgg::BindingProfile* base = profileRow(r, "base");
    ASSERT_TRUE(base);
    EXPECT_EQ(base->fieldEvals, 0u);  // pure source, no field work of its own
    EXPECT_GE(base->ms, 0.0);
    // `base` was pulled nested inside `m`: its time is NOT part of m's
    // (exclusive accounting), so both rows are small and independently listed.
}

TEST(EvalProfile, CacheHitRows) {
    pgg::MemoryCache cache;
    pgg::RunParams p;
    p.cache = &cache;
    p.profile = true;
    pgg::RunResult r1 = pgg::run(std::string(kGeo), p);
    pggtest::expectNoErrors(r1);
    ASSERT_FALSE(r1.stats.profile.empty());
    for (const pgg::BindingProfile& b : r1.stats.profile) EXPECT_FALSE(b.cacheHit);

    pgg::RunResult r2 = pgg::run(std::string(kGeo), p);
    pggtest::expectNoErrors(r2);
    ASSERT_EQ(r2.stats.profile.size(), r1.stats.profile.size());
    EXPECT_EQ(r2.stats.cacheMisses, 0u);
    for (const pgg::BindingProfile& b : r2.stats.profile) {
        EXPECT_TRUE(b.cacheHit) << b.name;
        EXPECT_EQ(b.fieldEvals, 0u) << b.name;
        EXPECT_GE(b.ms, 0.0) << b.name;
    }
}

TEST(EvalProfile, SortedByTimeThenName) {
    std::vector<pgg::BindingProfile> rows = {{"b", 1.0, 0, false}, {"a", 1.0, 0, false}, {"c", 2.0, 0, true}};
    const std::vector<pgg::BindingProfile> sorted = pgg::profileByTime(rows);
    ASSERT_EQ(sorted.size(), 3u);
    EXPECT_EQ(sorted[0].name, "c");
    EXPECT_EQ(sorted[1].name, "a");
    EXPECT_EQ(sorted[2].name, "b");
}

// --- --eval --------------------------------------------------------------------

TEST(EvalProfile, EvalFieldStatsExactText) {
    pgg::RunParams p;
    p.evals = {{"dot(@P, (0, 0, 0)) + 1", "base"}};
    pgg::RunResult r = pgg::run(std::string(kGeo), p);
    pggtest::expectNoErrors(r);
    // --eval never narrows the run: the declared output is still computed.
    EXPECT_EQ(r.outputs.size(), 1u);

    const pgg::ProbeRecord* rec = evalRecord(r, "base");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->inspector, "eval");
    EXPECT_EQ(rec->text,
              "dot(@P, (0, 0, 0)) + 1 = f32 on 42 pts\n"
              "__eval: mean 1, p50 1, p90 1, min 1, max 1 (42 pts)");
}

TEST(EvalProfile, EvalConstantBroadcasts) {
    pgg::RunParams p;
    p.evals = {{"41 + 1", "base"}};
    pgg::RunResult r = pgg::run(std::string(kGeo), p);
    pggtest::expectNoErrors(r);
    const pgg::ProbeRecord* rec = evalRecord(r, "base");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text,
              "41 + 1 = int on 42 pts\n"
              "__eval: mean 42, p50 42, p90 42, min 42, max 42 (42 pts)");
}

TEST(EvalProfile, EvalVecPrintsPerComponent) {
    pgg::RunParams p;
    p.evals = {{"@N * 0 + (1, 2, 3)", "base"}};  // exactly (1, 2, 3) per point, no -0.0
    pgg::RunResult r = pgg::run(std::string(kGeo), p);
    pggtest::expectNoErrors(r);
    const pgg::ProbeRecord* rec = evalRecord(r, "base");
    ASSERT_TRUE(rec);
    EXPECT_EQ(rec->text,
              "@N * 0 + (1, 2, 3) = vec3 on 42 pts\n"
              "__eval.x: mean 1, p50 1, p90 1, min 1, max 1 (42 pts)\n"
              "__eval.y: mean 2, p50 2, p90 2, min 2, max 2 (42 pts)\n"
              "__eval.z: mean 3, p50 3, p90 3, min 3, max 3 (42 pts)");
}

TEST(EvalProfile, EvalFieldOverNormals) {
    // The incident case: the spread of a field over a geometry without
    // editing the file. dot(@N, up) on a sphere has a near-zero mean and a
    // symmetric min/max spread (exact values depend on the sphere mesh, so
    // only the record structure is pinned here — the numerics are pinned by
    // the exact-text tests above).
    pgg::RunParams p;
    p.evals = {{"dot(@N, (0, 1, 0))", "base"}};
    pgg::RunResult r = pgg::run(std::string(kGeo), p);
    pggtest::expectNoErrors(r);
    const pgg::ProbeRecord* rec = evalRecord(r, "base");
    ASSERT_TRUE(rec);
    EXPECT_TRUE(rec->text.find("dot(@N, (0, 1, 0)) = f32 on 42 pts") == 0) << rec->text;
    EXPECT_TRUE(rec->text.find("\n__eval: mean ") != std::string::npos) << rec->text;
    EXPECT_TRUE(rec->text.find("(42 pts)") != std::string::npos) << rec->text;
}

TEST(EvalProfile, EvalTargetNotFound) {
    pgg::RunParams p;
    p.evals = {{"1", "nope"}};
    pgg::RunResult r = pgg::run(std::string(kGeo), p);
    EXPECT_TRUE(r.hasErrors());
    EXPECT_TRUE(hasMessage(r, "E606", "nope"));
}

TEST(EvalProfile, EvalBadExpression) {
    pgg::RunParams p;
    p.evals = {{"@P +", "base"}};
    pgg::RunResult r = pgg::run(std::string(kGeo), p);
    EXPECT_TRUE(r.hasErrors());
    EXPECT_TRUE(hasMessage(r, "E606", "bad expression"));
}

TEST(EvalProfile, EvalNonGeoTarget) {
    const std::string src =
        "s = 41 + 1\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "output base\n";
    pgg::RunParams p;
    p.evals = {{"1", "s"}};
    pgg::RunResult r = pgg::run(src, p);
    EXPECT_TRUE(r.hasErrors());
    EXPECT_TRUE(hasMessage(r, "E606", "not a geo value"));
}

TEST(EvalProfile, EvalStringExpressionRejected) {
    pgg::RunParams p;
    p.evals = {{"\"hi\"", "base"}};
    pgg::RunResult r = pgg::run(std::string(kGeo), p);
    EXPECT_TRUE(r.hasErrors());
    EXPECT_TRUE(hasMessage(r, "E606", "must be numeric or bool"));
}

}  // namespace
