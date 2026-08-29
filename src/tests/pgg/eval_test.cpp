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

// --- E2 acceptance corpus (spec §15 E2): rock + mask scatter + instances -----

const std::string kCorpusE2 = std::string(PGG_CORPUS_DIR) + "/e2_rock_scatter.pgg";

const pgg::Value* outputOf(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return &o.value;
    return nullptr;
}

TEST(EvalE2, CorpusScatterMatchesGoldens) {
    pgg::RunResult r = pgg::runFile(kCorpusE2);
    expectNoErrors(r);
    ASSERT_EQ(r.outputs.size(), 4u);

    // rock: ico subdiv 3 displaced (counts unchanged by the displace).
    pgg::GeoPtr rock = pgg::asGeo(*outputOf(r, "rock"));
    ASSERT_TRUE(rock);
    EXPECT_EQ(rock->kind, pgg::GeoKind::Mesh);
    EXPECT_EQ(rock->pointCount(), 642u);
    EXPECT_EQ(rock->faceCount(), 1280u);

    // Golden rock bbox (seed 42), also destructured in the corpus itself.
    const glm::vec3 mn = pgg::asVec3(*outputOf(r, "mn"));
    const glm::vec3 mx = pgg::asVec3(*outputOf(r, "mx"));
    EXPECT_NEAR(mn.x, -2.14426f, 1e-5f);
    EXPECT_NEAR(mn.y, -2.11947f, 1e-5f);
    EXPECT_NEAR(mn.z, -2.07377f, 1e-5f);
    EXPECT_NEAR(mx.x, 2.07115f, 1e-5f);
    EXPECT_NEAR(mx.y, 2.0f, 1e-5f);
    EXPECT_NEAR(mx.z, 2.20019f, 1e-5f);
    glm::vec3 gmn, gmx;
    pgg::geoBBox(*rock, gmn, gmx);
    EXPECT_EQ(gmn, mn);
    EXPECT_EQ(gmx, mx);

    // instances: 15 anchors on the flat mask, split 4/11 across 2 variants.
    // (E3: the poisson kept-set is the unique conflict-free set — re-pinned
    // from the E2 greedy values 25 / 10/15, spec §19 v1.0.)
    pgg::GeoPtr inst = pgg::asGeo(*outputOf(r, "inst"));
    ASSERT_TRUE(inst);
    EXPECT_EQ(inst->kind, pgg::GeoKind::Instances);
    ASSERT_EQ(inst->pointCount(), 15u);
    ASSERT_TRUE(inst->instanceSources);
    ASSERT_EQ(inst->instanceSources->size(), 2u);
    const pgg::AttrColumn* variantCol = inst->pointAttrs ? inst->pointAttrs->find("variant") : nullptr;
    ASSERT_TRUE(variantCol);
    const auto& variants = std::get<std::shared_ptr<const std::vector<int64_t>>>(variantCol->data);
    int v0 = 0, v1 = 0;
    for (int64_t v : *variants) (v == 0 ? v0 : v1) += 1;
    EXPECT_EQ(v0, 4);
    EXPECT_EQ(v1, 11);

    // Mask-driven density: every anchor sits on the flat top of the rock.
    for (const glm::vec3& p : *inst->positions) EXPECT_GT(p.y, 1.0f);
    // Golden anchor bbox (seed 42).
    glm::vec3 amn, amx;
    pgg::geoBBox(*inst, amn, amx);
    EXPECT_NEAR(amn.x, -1.31312f, 1e-5f);
    EXPECT_NEAR(amn.y, 1.52644f, 1e-5f);
    EXPECT_NEAR(amn.z, -0.999541f, 1e-4f);
    EXPECT_NEAR(amx.x, 0.832977f, 1e-5f);
    EXPECT_NEAR(amx.y, 1.98056f, 1e-5f);
    EXPECT_NEAR(amx.z, 1.18762f, 1e-5f);
}

TEST(EvalE2, CorpusDeterminismAcrossRuns) {
    pgg::RunResult a = pgg::runFile(kCorpusE2);
    pgg::RunResult b = pgg::runFile(kCorpusE2);
    expectNoErrors(a);
    expectNoErrors(b);
    EXPECT_EQ(*pgg::asGeo(*outputOf(a, "inst"))->positions, *pgg::asGeo(*outputOf(b, "inst"))->positions);
    EXPECT_EQ(*pgg::asGeo(*outputOf(a, "rock"))->positions, *pgg::asGeo(*outputOf(b, "rock"))->positions);
    // A different seed produces a different (deterministic) scatter.
    pgg::RunParams p;
    p.values.push_back({"seed", pgg::Value(int64_t(7))});
    pgg::RunResult c = pgg::runFile(kCorpusE2, p);
    expectNoErrors(c);
    pgg::RunResult c2 = pgg::runFile(kCorpusE2, p);
    EXPECT_EQ(*pgg::asGeo(*outputOf(c, "inst"))->positions, *pgg::asGeo(*outputOf(c2, "inst"))->positions);
}

TEST(EvalE2, CorpusMinDistHolds) {
    // The corpus scatters with min_dist = 0.35.
    pgg::RunResult r = pgg::runFile(kCorpusE2);
    expectNoErrors(r);
    pgg::GeoPtr inst = pgg::asGeo(*outputOf(r, "inst"));
    ASSERT_TRUE(inst);
    for (size_t i = 0; i < inst->pointCount(); ++i)
        for (size_t j = i + 1; j < inst->pointCount(); ++j)
            EXPECT_GE(glm::length((*inst->positions)[i] - (*inst->positions)[j]), 0.35f - 1e-4f)
                << i << " " << j;
}

}  // namespace
