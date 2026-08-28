// E1 acceptance tests (spec §15): the corpus scenario executes with golden
// stats; one field with three consumers on one geometry evaluates once
// (memoization counters); unused bindings and downstream subgraphs are never
// evaluated (laziness, N2); runs are deterministic.
#include <gtest/gtest.h>

#include "pgg/eval.h"

namespace {

const std::string kCorpus = std::string(PGG_CORPUS_DIR) + "/e1_rock.pgg";

uint64_t fieldEvals(const pgg::RunResult& r, const std::string& binding) {
    auto it = r.stats.bindingFieldEvals.find(binding);
    return it == r.stats.bindingFieldEvals.end() ? 0 : it->second;
}

void expectNoErrors(const pgg::RunResult& r) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
    EXPECT_FALSE(r.hasErrors());
}

TEST(Eval, CorpusRockScenarioMatchesGoldens) {
    pgg::RunResult r = pgg::runFile(kCorpus);
    expectNoErrors(r);
    ASSERT_EQ(r.outputs.size(), 1u);
    EXPECT_EQ(r.outputs[0].name, "rock");
    pgg::GeoPtr g = pgg::asGeo(r.outputs[0].value);
    ASSERT_EQ(g->kind, pgg::GeoKind::Mesh);
    // Ico subdiv 3: V = 10*4^3+2, F = 20*4^3.
    EXPECT_EQ(g->pointCount(), 642u);
    EXPECT_EQ(g->cornerCount(), 3840u);
    EXPECT_EQ(g->faceCount(), 1280u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    // compute_normals ran: unit, outward-facing normals.
    ASSERT_TRUE(g->normals);
    for (size_t i = 0; i < g->pointCount(); ++i) {
        EXPECT_NEAR(glm::length((*g->normals)[i]), 1.0f, 1e-4f);
        EXPECT_GT(glm::dot((*g->normals)[i], glm::normalize((*g->positions)[i])), 0.5f);
    }
    // Golden bbox of the displaced sphere (seed 42).
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    EXPECT_NEAR(mn.x, -1.19212f, 1e-5f);
    EXPECT_NEAR(mn.y, -1.32f, 1e-5f);
    EXPECT_NEAR(mn.z, -1.50273f, 1e-5f);
    EXPECT_NEAR(mx.x, 1.2555f, 1e-5f);
    EXPECT_NEAR(mx.y, 1.11226f, 1e-5f);
    EXPECT_NEAR(mx.z, 1.08165f, 1e-5f);
}

TEST(Eval, MemoizationOneFieldThreeConsumers) {
    // Corpus: `offset = @N * (n + n) * 0.35` and `where = n > 0.1` in one
    // set_position — the field n is consumed three times on one geometry
    // and must evaluate exactly once (§4.4 identity rule).
    pgg::RunResult r = pgg::runFile(kCorpus);
    expectNoErrors(r);
    EXPECT_EQ(fieldEvals(r, "n"), 1u);
}

TEST(Eval, LazinessUnusedBindingNotEvaluated) {
    const std::string src =
        "root_rng = rng_from_seed(1)\n"
        "noise_rng = split_rng(root_rng, key = \"n\")\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "n = fbm(scale = 2.0, rng = noise_rng)\n"
        "rock = set_position(base, offset = @N * n)\n"
        "unused = fbm(scale = 9.0, rng = noise_rng)\n"
        "output base\n"
        "output rock\n";
    pgg::RunResult all = pgg::run(src);
    expectNoErrors(all);
    EXPECT_EQ(all.outputs.size(), 2u);
    EXPECT_EQ(fieldEvals(all, "n"), 1u);
    EXPECT_EQ(fieldEvals(all, "unused"), 0u);  // never pulled -> never compiled/evaluated

    // Pulling a subgraph root does not evaluate nodes downstream of it (N2).
    pgg::RunResult mid = pgg::run(src, {}, {"base"});
    expectNoErrors(mid);
    ASSERT_EQ(mid.outputs.size(), 1u);
    EXPECT_EQ(mid.outputs[0].name, "base");
    EXPECT_EQ(fieldEvals(mid, "n"), 0u);
    EXPECT_EQ(mid.stats.fieldsEvaluated, 0u);
}

TEST(Eval, DeterminismAcrossRuns) {
    pgg::RunResult a = pgg::runFile(kCorpus);
    pgg::RunResult b = pgg::runFile(kCorpus);
    expectNoErrors(a);
    expectNoErrors(b);
    EXPECT_EQ(*pgg::asGeo(a.outputs[0].value)->positions, *pgg::asGeo(b.outputs[0].value)->positions);
    // Param override changes the stream deterministically (seed -> rng).
    pgg::RunParams p;
    p.values.push_back({"seed", pgg::Value(int64_t(7))});
    pgg::RunResult c = pgg::runFile(kCorpus, p);
    expectNoErrors(c);
    EXPECT_NE(*pgg::asGeo(a.outputs[0].value)->positions, *pgg::asGeo(c.outputs[0].value)->positions);
    pgg::RunResult c2 = pgg::runFile(kCorpus, p);
    EXPECT_EQ(*pgg::asGeo(c.outputs[0].value)->positions, *pgg::asGeo(c2.outputs[0].value)->positions);
}

}  // namespace
