// §8.8 distribute_points tests (E2 acceptance): mask-driven density (no
// points outside the mask), min_dist pairwise, determinism across runs,
// density response (zero -> zero, more density -> more points), attribute and
// group inheritance, uniform vs poisson.
#include <gtest/gtest.h>

#include "pgg/eval.h"

namespace {

void expectNoErrors(const pgg::RunResult& r) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
    EXPECT_FALSE(r.hasErrors());
}

pgg::GeoPtr geoOutput(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return pgg::asGeo(o.value);
    return nullptr;
}

const char* kSetup =
    "root_rng = rng_from_seed(7)\n"
    "scatter_rng = split_rng(root_rng, key = \"scatter\")\n"
    "b = ico_sphere(subdiv = 2, radius = 2.0)\n"
    "flat = mark(b, \"flat\", where = dot(@N, (0, 1, 0)) > 0.8)\n";

TEST(Scatter, MaskDrivesDensityNoPointsOutsideMask) {
    pgg::RunResult r = pgg::run(std::string(kSetup) +
                                "pts = distribute_points(flat, density = ingroup(\"flat\") * 6.0, min_dist = 0.3, rng = scatter_rng)\n"
                                "output pts\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "pts");
    ASSERT_TRUE(g);
    ASSERT_GT(g->pointCount(), 0u);
    // Every emitted point inherited the "flat" group (density is zero
    // elsewhere, so candidates outside the mask never survive thinning).
    ASSERT_TRUE(g->pointGroups);
    pgg::ConstBoolColumnPtr flatCol = g->pointGroups->find("flat");
    ASSERT_TRUE(flatCol);
    for (size_t i = 0; i < g->pointCount(); ++i) EXPECT_EQ((*flatCol)[i], 1) << i;
    // The mask is the sphere top: all points sit high up.
    for (const glm::vec3& p : *g->positions) EXPECT_GT(p.y, 1.0f);
}

TEST(Scatter, MinDistHoldsPairwise) {
    const float minDist = 0.45f;
    pgg::RunResult r = pgg::run(std::string(kSetup) +
                                "pts = distribute_points(flat, density = ingroup(\"flat\") * 8.0, min_dist = 0.45, rng = scatter_rng)\n"
                                "output pts\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "pts");
    ASSERT_TRUE(g);
    ASSERT_GT(g->pointCount(), 1u);
    for (size_t i = 0; i < g->pointCount(); ++i)
        for (size_t j = i + 1; j < g->pointCount(); ++j)
            EXPECT_GE(glm::length((*g->positions)[i] - (*g->positions)[j]), minDist - 1e-4f)
                << i << " " << j;
}

TEST(Scatter, DeterminismAcrossRuns) {
    const std::string src = std::string(kSetup) +
                            "pts = distribute_points(flat, density = ingroup(\"flat\") * 6.0, min_dist = 0.3, rng = scatter_rng)\n"
                            "output pts\n";
    pgg::RunResult a = pgg::run(src);
    pgg::RunResult b = pgg::run(src);
    expectNoErrors(a);
    expectNoErrors(b);
    EXPECT_EQ(*geoOutput(a, "pts")->positions, *geoOutput(b, "pts")->positions);

    // A different rng gives a different (but itself deterministic) result.
    const std::string src2 =
        "root_rng = rng_from_seed(8)\n"
        "scatter_rng = split_rng(root_rng, key = \"scatter\")\n"
        "b = ico_sphere(subdiv = 2, radius = 2.0)\n"
        "flat = mark(b, \"flat\", where = dot(@N, (0, 1, 0)) > 0.8)\n"
        "pts = distribute_points(flat, density = ingroup(\"flat\") * 6.0, min_dist = 0.3, rng = scatter_rng)\n"
        "output pts\n";
    pgg::RunResult c = pgg::run(src2);
    expectNoErrors(c);
    EXPECT_NE(*geoOutput(a, "pts")->positions, *geoOutput(c, "pts")->positions);
}

TEST(Scatter, DensityResponseZeroAndMore) {
    // Zero density -> zero points.
    pgg::RunResult zero = pgg::run(
        "root_rng = rng_from_seed(7)\n"
        "b = grid(size = (4, 4), res = 8)\n"
        "pts = distribute_points(b, density = 0.0, rng = root_rng)\n"
        "output pts\n");
    expectNoErrors(zero);
    EXPECT_EQ(geoOutput(zero, "pts")->pointCount(), 0u);

    // More density -> more candidates (uniform mode keeps them all).
    pgg::RunResult lo = pgg::run(
        "root_rng = rng_from_seed(7)\n"
        "b = grid(size = (4, 4), res = 8)\n"
        "pts = distribute_points(b, density = 2.0, mode = uniform, rng = root_rng)\n"
        "output pts\n");
    pgg::RunResult hi = pgg::run(
        "root_rng = rng_from_seed(7)\n"
        "b = grid(size = (4, 4), res = 8)\n"
        "pts = distribute_points(b, density = 4.0, mode = uniform, rng = root_rng)\n"
        "output pts\n");
    expectNoErrors(lo);
    expectNoErrors(hi);
    const size_t nLo = geoOutput(lo, "pts")->pointCount();
    const size_t nHi = geoOutput(hi, "pts")->pointCount();
    EXPECT_GT(nLo, 0u);
    EXPECT_GT(nHi, nLo);
}

TEST(Scatter, UniformVsPoisson) {
    // Same density/rng: poisson with min_dist only ever removes points.
    const std::string base =
        "root_rng = rng_from_seed(7)\n"
        "scatter_rng = split_rng(root_rng, key = \"s\")\n"
        "b = grid(size = (4, 4), res = 8)\n";
    pgg::RunResult uni = pgg::run(base +
                                  "pts = distribute_points(b, density = 3.0, mode = uniform, rng = scatter_rng)\n"
                                  "output pts\n");
    pgg::RunResult poi = pgg::run(base +
                                  "pts = distribute_points(b, density = 3.0, mode = poisson, min_dist = 0.3, rng = scatter_rng)\n"
                                  "output pts\n");
    expectNoErrors(uni);
    expectNoErrors(poi);
    EXPECT_GT(geoOutput(uni, "pts")->pointCount(), geoOutput(poi, "pts")->pointCount());
}

TEST(Scatter, InheritsAttributesGroupsAndNormals) {
    pgg::RunResult r = pgg::run(
        "root_rng = rng_from_seed(7)\n"
        "scatter_rng = split_rng(root_rng, key = \"s\")\n"
        "b = grid(size = (4, 4), res = 4)\n"
        "s = set(b, \"xc\", dot(@P, (1, 0, 0)))\n"
        "m = mark(s, \"right\", where = dot(@P, (1, 0, 0)) > 0)\n"
        "pts = distribute_points(m, density = 3.0, mode = uniform, rng = scatter_rng)\n"
        "output pts\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "pts");
    ASSERT_TRUE(g);
    ASSERT_GT(g->pointCount(), 0u);
    // Point attribute inherited barycentrically: xc ~= position x.
    const pgg::AttrColumn* xc = g->pointAttrs ? g->pointAttrs->find("xc") : nullptr;
    ASSERT_TRUE(xc);
    const auto& vals = std::get<std::shared_ptr<const std::vector<float>>>(xc->data);
    ASSERT_EQ(vals->size(), g->pointCount());
    for (size_t i = 0; i < g->pointCount(); ++i)
        EXPECT_NEAR((*vals)[i], (*g->positions)[i].x, 1e-4f) << i;
    // Groups inherited by the > 0.5 sample rule, normals present.
    ASSERT_TRUE(g->pointGroups);
    EXPECT_TRUE(g->pointGroups->find("right"));
    ASSERT_TRUE(g->normals);
    EXPECT_EQ(g->normals->size(), g->pointCount());
    // Points inside a face all lie on the surface: y == 0 exactly.
    for (const glm::vec3& p : *g->positions) EXPECT_FLOAT_EQ(p.y, 0.0f);
}

}  // namespace
