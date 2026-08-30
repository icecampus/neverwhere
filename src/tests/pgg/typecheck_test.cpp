// Static typecheck tests (§11.2): one case per E2xx code, E604, E605, the
// §6.7 output rules, and the deferred-stage diagnostics.
#include <gtest/gtest.h>

#include "pgg/eval.h"

namespace {

int countCode(const pgg::RunResult& r, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

pgg::RunResult runSrc(const std::string& src) { return pgg::run(src); }

TEST(Typecheck, CleanFilePasses) {
    pgg::RunResult r = runSrc(
        "root_rng = rng_from_seed(42)\n"
        "noise_rng = split_rng(root_rng, key = \"surface\")\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "n = fbm(scale = 2.5, octaves = 5, rng = noise_rng)\n"
        "rock = set_position(base, offset = @N * n * 0.35, where = n > 0.1)\n"
        "out = compute_normals(rock)\n"
        "output out\n");
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
    EXPECT_FALSE(r.hasErrors());
}

TEST(Typecheck, E201UnknownOperation) {
    pgg::RunResult r = runSrc("x = no_such_op(1)\noutput x\n");
    EXPECT_EQ(countCode(r, "E201"), 1);
    EXPECT_TRUE(r.hasErrors());
}

TEST(Typecheck, E202Arity) {
    // Missing required argument.
    EXPECT_EQ(countCode(runSrc("x = ico_sphere(subdiv = 1)\noutput x\n"), "E202"), 1);
    // Too many positional arguments.
    EXPECT_EQ(countCode(runSrc("x = ico_sphere(1, 1.0, 3)\noutput x\n"), "E202"), 1);
    // Same argument twice.
    EXPECT_EQ(countCode(runSrc("x = ico_sphere(1, subdiv = 2, radius = 1.0)\noutput x\n"), "E202"), 1);
}

TEST(Typecheck, E203UnknownParameter) {
    pgg::RunResult r = runSrc("x = ico_sphere(subdiv = 1, radius = 1.0, bogus = 2)\noutput x\n");
    EXPECT_EQ(countCode(r, "E203"), 1);
}

TEST(Typecheck, E204TypeMismatch) {
    // vec3 where f32 is required.
    pgg::RunResult r = runSrc("x = ico_sphere(subdiv = 1, radius = (1, 2, 3))\noutput x\n");
    EXPECT_EQ(countCode(r, "E204"), 1);
    // f32 where int is required (narrowing needs a cast).
    EXPECT_EQ(countCode(runSrc("x = ico_sphere(subdiv = 1.5, radius = 1.0)\noutput x\n"), "E204"), 1);
    // Ordered comparison of vectors.
    EXPECT_EQ(countCode(runSrc(
                          "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
                          "m = set_position(b, where = @P > (0, 0, 0))\n"
                          "output m\n"),
                      "E204"),
              1);
}

TEST(Typecheck, E205FieldAsValue) {
    pgg::RunResult r = runSrc(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = transform(b, translate = @P)\n"
        "output m\n");
    EXPECT_EQ(countCode(r, "E205"), 1);
}

TEST(Typecheck, E206EnumValue) {
    pgg::RunResult r = runSrc(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "n = compute_normals(b, mode = fancy)\n"
        "output n\n");
    EXPECT_EQ(countCode(r, "E206"), 1);
    // Legal enum values pass.
    pgg::RunResult ok = runSrc(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "n = compute_normals(b, mode = by_angle)\n"
        "output n\n");
    EXPECT_EQ(countCode(ok, "E206"), 0);
    EXPECT_FALSE(ok.hasErrors());
}

TEST(Typecheck, E604UnboundParam) {
    const std::string src =
        "param seed: int\n"
        "r = rng_from_seed(seed)\n"
        "b = point_cloud(count = 1, bounds = (1, 1, 1), rng = r)\n"
        "output b\n";
    pgg::RunResult unbound = pgg::run(src);
    EXPECT_EQ(countCode(unbound, "E604"), 1);
    // Bound at launch: no E604, the graph runs.
    pgg::RunParams params;
    params.values.push_back({"seed", pgg::Value(int64_t(7))});
    pgg::RunResult bound = pgg::run(src, params);
    EXPECT_EQ(countCode(bound, "E604"), 0);
    EXPECT_FALSE(bound.hasErrors());
    // Default value also satisfies E604.
    pgg::RunResult withDefault = pgg::run(
        "param seed: int = 3\n"
        "r = rng_from_seed(seed)\n"
        "b = point_cloud(count = 1, bounds = (1, 1, 1), rng = r)\n"
        "output b\n");
    EXPECT_EQ(countCode(withDefault, "E604"), 0);
    EXPECT_FALSE(withDefault.hasErrors());
}

TEST(Typecheck, E605NoOutput) {
    pgg::RunResult r = runSrc("x = 1\n");
    EXPECT_EQ(countCode(r, "E605"), 1);
}

TEST(Typecheck, OutputRejectsFieldAndRng) {
    pgg::RunResult f = runSrc(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "n = fbm(rng = rng_from_seed(1))\n"
        "output n\n");
    EXPECT_EQ(countCode(f, "E204"), 1);
    pgg::RunResult g = runSrc("r = rng_from_seed(1)\noutput r\n");
    EXPECT_EQ(countCode(g, "E204"), 1);
    // Scalar and vec outputs are legal roots.
    pgg::RunResult ok = runSrc("x = 1.5\nv = (1, 2, 3)\noutput x\noutput v\n");
    EXPECT_FALSE(ok.hasErrors());
}

TEST(Typecheck, DeferredOperationsReportCleanly) {
    // Known catalog names past E4: precise "not supported" diagnostic, no crash.
    pgg::RunResult m = runSrc(
        "a = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "b = subdivide(a, level = 2)\n"
        "output b\n");
    EXPECT_EQ(countCode(m, "E201"), 1);
    pgg::RunResult im = runSrc("m = import_mesh(uri = \"rock.obj\")\noutput m\n");
    EXPECT_EQ(countCode(im, "E201"), 1);
    pgg::RunResult rc = runSrc(
        "a = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "r = raycast(a, b)\n"
        "output r\n");
    EXPECT_EQ(countCode(rc, "E201"), 1);
}

TEST(Typecheck, UnsupportedFileShapesReportCleanly) {
    // def declarations, imports, zones parse (E0 grammar) but must not run.
    pgg::RunResult def = runSrc(
        "def f(x: geo) -> (o: geo) {\n"
        "    o = x\n"
        "}\n"
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "output b\n");
    EXPECT_EQ(countCode(def, "E201"), 1);
    pgg::RunResult imp = runSrc(
        "import lib.noise\n"
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "output b\n");
    EXPECT_EQ(countCode(imp, "E201"), 1);
    pgg::RunResult zone = runSrc(
        "r = repeat (1, iterations = 2) |cur| {\n"
        "    cur = cur + 1\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countCode(zone, "E201"), 1);
    pgg::RunResult fe = runSrc(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "f = foreach piece in b {\n"
        "    q = b\n"
        "}\n"
        "output f\n");
    EXPECT_EQ(countCode(fe, "E201"), 1);
}

TEST(Typecheck, QualifiedCallsAreStageE5) {
    pgg::RunResult r = runSrc(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = lib.make_rock(size = 1.0)\n"
        "output m\n");
    EXPECT_EQ(countCode(r, "E201"), 1);
}

TEST(Typecheck, UnknownAttributeIsE302) {
    pgg::RunResult r = runSrc(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = set_position(b, offset = @N * @slope)\n"
        "output m\n");
    EXPECT_EQ(countCode(r, "E302"), 1);
}

}  // namespace
