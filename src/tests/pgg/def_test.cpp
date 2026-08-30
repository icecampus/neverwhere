// E5 acceptance tests (spec §7, §15): def calls are inlined into a flat
// graph — positional/keyword/default binding, multi-output destructuring,
// nested defs, expression lifting, hermeticity (E105), recursion (E503), the
// static/transitive rng discipline (E401), instance independence, and the rng
// criteria (same rng -> same result; explicit split_rng -> stable variants).
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/pgg.h"
#include "test_utils.h"

namespace {

int countCode(const pgg::RunResult& r, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

int countCode(const pgg::Document& d, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& x : d.diagnostics)
        if (x.code == code) n += 1;
    return n;
}

bool hasMessage(const pgg::RunResult& r, const std::string& code, const std::string& needle) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code && d.message.find(needle) != std::string::npos) return true;
    return false;
}

pgg::RunResult runSrc(const std::string& src) { return pgg::run(src); }

// --- call semantics ------------------------------------------------------------

TEST(Def, PositionalKwDefaults) {
    pgg::RunResult r = runSrc(
        "def add(a: f32, b: f32 = 2.0) -> (out: f32) {\n"
        "    \"\"\"Sum.\"\"\"\n"
        "    out = a + b\n"
        "}\n"
        "x = add(1.0)\n"
        "y = add(1.0, b = 4.0)\n"
        "z = add(a = 3.0)\n"
        "output x\n"
        "output y\n"
        "output z\n");
    pggtest::expectNoErrors(r);
    EXPECT_EQ(pgg::asF32(*pggtest::outputOf(r, "x")), 3.0f);
    EXPECT_EQ(pgg::asF32(*pggtest::outputOf(r, "y")), 5.0f);
    EXPECT_EQ(pgg::asF32(*pggtest::outputOf(r, "z")), 5.0f);
}

TEST(Def, CallBindingErrors) {
    // Missing required argument, unknown parameter, type mismatch against the
    // declared interface, tuple call without destructuring, destructure arity.
    EXPECT_EQ(countCode(runSrc(
        "def req(a: f32) -> (out: f32) {\n"
        "    \"\"\"R.\"\"\"\n"
        "    out = a\n"
        "}\n"
        "x = req()\n"
        "output x\n"), "E202"), 1);
    EXPECT_EQ(countCode(runSrc(
        "def req(a: f32) -> (out: f32) {\n"
        "    \"\"\"R.\"\"\"\n"
        "    out = a\n"
        "}\n"
        "x = req(a = 1.0, bogus = 2.0)\n"
        "output x\n"), "E203"), 1);
    pgg::RunResult mismatch = runSrc(
        "def sized(size: f32) -> (out: f32) {\n"
        "    \"\"\"S.\"\"\"\n"
        "    out = size\n"
        "}\n"
        "b = box(size = (1, 1, 1))\n"
        "x = sized(size = b)\n"
        "output x\n");
    EXPECT_EQ(countCode(mismatch, "E204"), 1);
    EXPECT_TRUE(hasMessage(mismatch, "E204", "def interface"));
    pgg::RunResult tuple = runSrc(
        "def pair() -> (a: f32, b: f32) {\n"
        "    \"\"\"P.\"\"\"\n"
        "    a = 1.0\n"
        "    b = 2.0\n"
        "}\n"
        "x = pair()\n"
        "output x\n");
    EXPECT_EQ(countCode(tuple, "E204"), 1);
    EXPECT_TRUE(hasMessage(tuple, "E204", "destructured"));
    EXPECT_EQ(countCode(runSrc(
        "def pair() -> (a: f32, b: f32) {\n"
        "    \"\"\"P.\"\"\"\n"
        "    a = 1.0\n"
        "    b = 2.0\n"
        "}\n"
        "x, y, z = pair()\n"
        "output x\n"), "E202"), 1);
}

TEST(Def, MultiOutputDestructuring) {
    pgg::RunResult r = runSrc(
        "def bounds(g: geo) -> (lo: vec3, hi: vec3) {\n"
        "    \"\"\"bbox through a def boundary.\"\"\"\n"
        "    lo, hi = bbox(g)\n"
        "}\n"
        "b = box(size = (2, 2, 2))\n"
        "mn, mx = bounds(b)\n"
        "output mn\n"
        "output mx\n");
    pggtest::expectNoErrors(r);
    pggtest::expectVec3Near(pgg::asVec3(*pggtest::outputOf(r, "mn")), glm::vec3(-1.0f));
    pggtest::expectVec3Near(pgg::asVec3(*pggtest::outputOf(r, "mx")), glm::vec3(1.0f));
}

TEST(Def, NestedDefsAndFieldClosure) {
    // §7.2 verbatim: a field argument evaluates in the consumption context
    // inside the def (@N of the geometry passed in).
    pgg::RunResult r = runSrc(
        "def displace(geo: geo, amount: field<f32>) -> (out: geo) {\n"
        "    \"\"\"Offset along normals by an arbitrary element function.\"\"\"\n"
        "    out = set_position(geo, offset = @N * amount)\n"
        "}\n"
        "def bump(geo: geo, k: f32, rng: rng) -> (out: geo) {\n"
        "    \"\"\"fbm bump through the nested displace.\"\"\"\n"
        "    out = displace(geo, amount = fbm(scale = 2.5, rng = rng) * k)\n"
        "}\n"
        "root = rng_from_seed(42)\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "rock = bump(base, k = 0.1, rng = root)\n"
        "output base\n"
        "output rock\n");
    pggtest::expectNoErrors(r);
    pgg::GeoPtr g = pggtest::geoOutput(r, "rock");
    ASSERT_TRUE(g != nullptr);
    EXPECT_EQ(g->pointCount(), 42u);  // ico subdiv 1, displaced but not retopologized
    // The field closure evaluated on the geometry inside the def: the
    // displaced surface differs from the base.
    EXPECT_NE(*g->positions, *pggtest::geoOutput(r, "base")->positions);
}

TEST(Def, ExpressionLifting) {
    // A def call nested inside another call's arguments is lifted into a
    // generated binding; instances still get fresh counters per call site.
    pgg::RunResult r = runSrc(
        "def one_box(size: f32 = 1.0) -> (out: geo<mesh>) {\n"
        "    \"\"\"One box.\"\"\"\n"
        "    out = box(size = vec3(size, size, size))\n"
        "}\n"
        "scene = merge(one_box(2.0), transform(one_box(3.0), translate = (10, 0, 0)))\n"
        "output scene\n");
    pggtest::expectNoErrors(r);
    pgg::GeoPtr g = pggtest::geoOutput(r, "scene");
    ASSERT_TRUE(g != nullptr);
    EXPECT_EQ(g->pointCount(), 16u);
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    pggtest::expectF32Near(mn.x, -1.0f);   // one_box(2.0) spans -1..1
    pggtest::expectF32Near(mx.x, 11.5f);   // one_box(3.0) spans 8.5..11.5
}

TEST(Def, InstancesAreIndependent) {
    // Two calls of one def with different arguments: the instance bindings do
    // not share state (the second call must not see the first's parameter).
    pgg::RunResult r = runSrc(
        "def one_box(size: f32) -> (out: geo<mesh>) {\n"
        "    \"\"\"One box.\"\"\"\n"
        "    out = box(size = vec3(size, size, size))\n"
        "}\n"
        "a = one_box(1.0)\n"
        "b = one_box(3.0)\n"
        "output a\n"
        "output b\n");
    pggtest::expectNoErrors(r);
    glm::vec3 mn, mx;
    pgg::geoBBox(*pggtest::geoOutput(r, "a"), mn, mx);
    pggtest::expectF32Near(mx.x, 0.5f);
    pgg::geoBBox(*pggtest::geoOutput(r, "b"), mn, mx);
    pggtest::expectF32Near(mx.x, 1.5f);
}

// --- hermeticity and def-graph checks --------------------------------------------

TEST(Def, HermeticityIsE105) {
    // A def body cannot capture top-level params/rng/bindings of its file.
    pgg::Document captureBinding = pgg::parse(
        "top = 5\n"
        "def bad(x: f32) -> (out: f32) {\n"
        "    \"\"\"B.\"\"\"\n"
        "    out = x + top\n"
        "}\n"
        "y = bad(1.0)\n"
        "output y\n");
    EXPECT_EQ(countCode(captureBinding, "E105"), 1);
    pgg::Document captureParam = pgg::parse(
        "param seed: int = 1\n"
        "def bad(x: f32) -> (out: f32) {\n"
        "    \"\"\"B.\"\"\"\n"
        "    out = x + seed\n"
        "}\n"
        "y = bad(1.0)\n"
        "output y\n");
    EXPECT_EQ(countCode(captureParam, "E105"), 1);
    pgg::Document captureRng = pgg::parse(
        "root = rng_from_seed(1)\n"
        "def bad(n: int) -> (out: geo) {\n"
        "    \"\"\"B.\"\"\"\n"
        "    out = point_cloud(count = n, bounds = (1, 1, 1), rng = root)\n"
        "}\n"
        "p = bad(2)\n"
        "output p\n");
    EXPECT_EQ(countCode(captureRng, "E105"), 1);
    // Defs calling defs stay free names — no E105, no E103.
    pgg::Document ok = pgg::parse(
        "def inner(x: f32) -> (out: f32) {\n"
        "    \"\"\"I.\"\"\"\n"
        "    out = x * 2.0\n"
        "}\n"
        "def outer(x: f32) -> (out: f32) {\n"
        "    \"\"\"O.\"\"\"\n"
        "    out = inner(x = x)\n"
        "}\n"
        "y = outer(1.0)\n"
        "output y\n");
    EXPECT_EQ(countCode(ok, "E105"), 0);
    EXPECT_EQ(countCode(ok, "E103"), 0);
}

TEST(Def, RecursionIsE503) {
    pgg::RunResult self = runSrc(
        "def a(x: f32) -> (out: f32) {\n"
        "    \"\"\"A.\"\"\"\n"
        "    out = a(x = x)\n"
        "}\n"
        "y = a(1.0)\n"
        "output y\n");
    EXPECT_EQ(countCode(self, "E503"), 1);
    pgg::RunResult indirect = runSrc(
        "def a(x: f32) -> (out: f32) {\n"
        "    \"\"\"A.\"\"\"\n"
        "    out = b(x = x)\n"
        "}\n"
        "def b(x: f32) -> (out: f32) {\n"
        "    \"\"\"B.\"\"\"\n"
        "    out = a(x = x)\n"
        "}\n"
        "y = a(1.0)\n"
        "output y\n");
    EXPECT_EQ(countCode(indirect, "E503"), 1);
}

TEST(Def, StochasticDefWithoutRngIsE401) {
    // Direct: rng_from_seed is an entropy source inside the def, so the rng
    // does not flow through the signature.
    pgg::RunResult direct = runSrc(
        "def bad(n: int) -> (out: geo) {\n"
        "    \"\"\"B.\"\"\"\n"
        "    r = rng_from_seed(42)\n"
        "    out = point_cloud(count = n, bounds = (1, 1, 1), rng = r)\n"
        "}\n"
        "p = bad(2)\n"
        "output p\n");
    EXPECT_EQ(countCode(direct, "E401"), 1);
    // Transitive: a def calling a stochastic def is stochastic itself (§7.3).
    pgg::RunResult transitive = runSrc(
        "def inner(n: int) -> (out: geo) {\n"
        "    \"\"\"I.\"\"\"\n"
        "    r = rng_from_seed(42)\n"
        "    out = point_cloud(count = n, bounds = (1, 1, 1), rng = r)\n"
        "}\n"
        "def outer(n: int) -> (out: geo) {\n"
        "    \"\"\"O.\"\"\"\n"
        "    out = inner(n = n)\n"
        "}\n"
        "p = outer(2)\n"
        "output p\n");
    EXPECT_EQ(countCode(transitive, "E401"), 2);
    // Clean: the generator flows through the signature — no E401, runs fine.
    pgg::RunResult clean = runSrc(
        "def good(n: int, rng: rng) -> (out: geo) {\n"
        "    \"\"\"G.\"\"\"\n"
        "    out = point_cloud(count = n, bounds = (1, 1, 1), rng = rng)\n"
        "}\n"
        "root = rng_from_seed(1)\n"
        "p = good(n = 2, rng = root)\n"
        "output p\n");
    pggtest::expectNoErrors(clean);
    EXPECT_EQ(countCode(clean, "E401"), 0);
}

// --- rng criteria (§15 E5 row) ------------------------------------------------------

const char* kCloudSrc =
    "def cloud(n: int, rng: rng) -> (out: geo<points>) {\n"
    "    \"\"\"Uniform points in a unit box.\"\"\"\n"
    "    out = point_cloud(count = n, bounds = (1, 1, 1), rng = rng)\n"
    "}\n"
    "root = rng_from_seed(7)\n"
    "a = cloud(n = 4, rng = root)\n"
    "b = cloud(n = 4, rng = root)\n"
    "output a\n"
    "output b\n";

TEST(Def, SameRngReproducesSameResult) {
    // Intentional repeat (§7.3): one generator on two instances — identical.
    pgg::RunResult r = runSrc(kCloudSrc);
    pggtest::expectNoErrors(r);
    EXPECT_EQ(*pggtest::geoOutput(r, "a")->positions, *pggtest::geoOutput(r, "b")->positions);
    // ... and stable across runs.
    pgg::RunResult r2 = runSrc(kCloudSrc);
    EXPECT_EQ(*pggtest::geoOutput(r, "a")->positions, *pggtest::geoOutput(r2, "a")->positions);
}

TEST(Def, SplitRngGivesStableDistinctVariants) {
    const std::string src =
        "def cloud(n: int, rng: rng) -> (out: geo<points>) {\n"
        "    \"\"\"Uniform points in a unit box.\"\"\"\n"
        "    out = point_cloud(count = n, bounds = (1, 1, 1), rng = rng)\n"
        "}\n"
        "root = rng_from_seed(7)\n"
        "a_rng = split_rng(root, key = \"a\")\n"
        "b_rng = split_rng(root, key = \"b\")\n"
        "a = cloud(n = 4, rng = a_rng)\n"
        "b = cloud(n = 4, rng = b_rng)\n"
        "output a\n"
        "output b\n";
    pgg::RunResult r = pgg::run(src);
    pggtest::expectNoErrors(r);
    EXPECT_NE(*pggtest::geoOutput(r, "a")->positions, *pggtest::geoOutput(r, "b")->positions);
    pgg::RunResult r2 = pgg::run(src);
    EXPECT_EQ(*pggtest::geoOutput(r, "a")->positions, *pggtest::geoOutput(r2, "a")->positions);
    EXPECT_EQ(*pggtest::geoOutput(r, "b")->positions, *pggtest::geoOutput(r2, "b")->positions);
}

}  // namespace
