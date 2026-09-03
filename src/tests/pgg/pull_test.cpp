// Value pulls (RunParams::pulls -> RunResult::pulled): the viewer-preview
// path to the value of any binding, not just declared outputs. Resolution
// follows the probe-path rules minus terminals: flat binding, instance path
// (every output), <ipath>.<local>; index-less def and attr terminals are
// E606. Pulls are lazy roots (a mid-graph pull does not compute the tail)
// and suppress declared outputs like probes do.
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "test_utils.h"

namespace {

int countCode(const pgg::RunResult& r, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

const pgg::RunOutput* findPulled(const pgg::RunResult& r, const std::string& name) {
    for (const pgg::RunOutput& o : r.pulled)
        if (o.name == name) return &o;
    return nullptr;
}

const std::string kGraph =
    "g = rng_from_seed(1)\n"
    "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
    "n = fbm(scale = 2.0, octaves = 2, rng = g)\n"
    "bumped = set_position(base, offset = @N * n * 0.1)\n"
    "tail = set(bumped, \"slope\", 1 - dot(@N, (0, 1, 0)))\n"
    "output tail\n";

TEST(Pull, FlatBindingValueAndOutputSuppression) {
    pgg::RunParams p;
    p.pulls = {"base"};
    pgg::RunResult r = pgg::run(kGraph, p);
    pggtest::expectNoErrors(r);
    EXPECT_TRUE(r.outputs.empty());  // pull-only run: declared outputs suppressed
    ASSERT_EQ(r.pulled.size(), 1u);
    EXPECT_EQ(r.pulled[0].name, "base");
    ASSERT_EQ(pgg::valueBase(r.pulled[0].value), pgg::ScalarType::Geo);
    EXPECT_EQ(pgg::asGeo(r.pulled[0].value)->pointCount(), 42u);
    // Laziness: the tail (a field binding) was never evaluated.
    auto it = r.stats.bindingFieldEvals.find("tail");
    EXPECT_TRUE(it == r.stats.bindingFieldEvals.end() || it->second == 0);
}

TEST(Pull, ExplicitOutputsKeepOutputs) {
    pgg::RunParams p;
    p.pulls = {"bumped"};
    pgg::RunResult r = pgg::run(kGraph, p, {"tail"});
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.outputs.size(), 1u);
    ASSERT_EQ(r.pulled.size(), 1u);
    EXPECT_EQ(r.pulled[0].name, "bumped");
}

TEST(Pull, FieldBindingIsE606) {
    pgg::RunParams p;
    p.pulls = {"n"};
    pgg::RunResult r = pgg::run(kGraph, p);
    EXPECT_EQ(countCode(r, "E606"), 1);
    EXPECT_TRUE(r.pulled.empty());
}

TEST(Pull, UnknownPathIsE606BeforeAnyEvaluation) {
    pgg::RunParams p;
    p.pulls = {"nope"};
    pgg::RunResult r = pgg::run(kGraph, p);
    EXPECT_EQ(countCode(r, "E606"), 1);
    EXPECT_EQ(r.stats.fieldsEvaluated, 0u);
}

const std::string kDefs =
    "def two(r: f32 = 1.0) -> (a: geo<mesh>, b: geo<points>) {\n"
    "    \"\"\"two outputs\"\"\"\n"
    "    raw = ico_sphere(subdiv = 1, radius = r)\n"
    "    a = compute_normals(raw)\n"
    "    b = mesh_line(count = 4, length = 2.0)\n"
    "}\n"
    "x, y = two(r = 2.0)\n"
    "x2, y2 = two(r = 3.0)\n"
    "output x\n";

TEST(Pull, InstancePathPullsEveryOutput) {
    pgg::RunParams p;
    p.pulls = {"two[1]"};
    pgg::RunResult r = pgg::run(kDefs, p);
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.pulled.size(), 2u);
    EXPECT_EQ(r.pulled[0].name, "two[1].a");
    EXPECT_EQ(r.pulled[1].name, "two[1].b");
    EXPECT_EQ(pgg::asGeo(r.pulled[0].value)->kind, pgg::GeoKind::Mesh);
    EXPECT_EQ(pgg::asGeo(r.pulled[1].value)->pointCount(), 4u);
}

TEST(Pull, InstanceLocalBinding) {
    pgg::RunParams p;
    p.pulls = {"two[0].raw"};
    pgg::RunResult r = pgg::run(kDefs, p);
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.pulled.size(), 1u);
    EXPECT_EQ(r.pulled[0].name, "two[0].raw");
    ASSERT_TRUE(findPulled(r, "two[0].raw"));
    EXPECT_EQ(pgg::asGeo(r.pulled[0].value)->kind, pgg::GeoKind::Mesh);
    // Instance-local pulls do not leak the sibling instance's outputs.
    EXPECT_FALSE(findPulled(r, "two[1].raw"));
}

TEST(Pull, IndexLessDefIsE606) {
    pgg::RunParams p;
    p.pulls = {"two"};
    pgg::RunResult r = pgg::run(kDefs, p);
    EXPECT_EQ(countCode(r, "E606"), 1);
}

}  // namespace
