// E5 contract tests (spec §7.4): expect/ensure are validator checks with
// author-written messages — attr-form contracts are decided statically
// against the schema when provable and at runtime on open schemas, condition
// contracts are constant-folded when literal and otherwise evaluated by the
// engine at instance pulls (E303/E304), with the instance path in the
// message (basic §9.5 chain).
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

bool hasMessage(const pgg::RunResult& r, const std::string& code, const std::string& needle) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code && d.message.find(needle) != std::string::npos) return true;
    return false;
}

pgg::RunResult runSrc(const std::string& src) { return pgg::run(src); }

TEST(Contract, ExpectAttrFormStaticE303) {
    // The §7.4 example: the schema proves @height absent — before execution,
    // with the author's hint text and the instance path.
    pgg::RunResult r = runSrc(
        "def erode(terrain: geo, rng: rng) -> (out: geo) {\n"
        "    \"\"\"Erosion stub.\"\"\"\n"
        "    expect terrain has @height: \"erode работает с хайтфилдом, см. make_heightfield\"\n"
        "    out = terrain\n"
        "}\n"
        "root = rng_from_seed(1)\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = erode(base, rng = root)\n"
        "output m\n");
    EXPECT_EQ(countCode(r, "E303"), 1);
    EXPECT_TRUE(hasMessage(r, "E303", "erode работает с хайтфилдом"));
    EXPECT_TRUE(hasMessage(r, "E303", "[instance erode[0]]"));
    EXPECT_TRUE(r.outputs.empty());  // static: before execution
    // Positive: the attribute is there — clean run.
    pgg::RunResult ok = runSrc(
        "def erode(terrain: geo, rng: rng) -> (out: geo) {\n"
        "    \"\"\"Erosion stub.\"\"\"\n"
        "    expect terrain has @height: \"erode работает с хайтфилдом, см. make_heightfield\"\n"
        "    out = terrain\n"
        "}\n"
        "root = rng_from_seed(1)\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "heighted = set(base, \"height\", 1.0)\n"
        "m = erode(heighted, rng = root)\n"
        "output m\n");
    pggtest::expectNoErrors(ok);
}

TEST(Contract, ExpectAttrFormRuntimeOnOpenSchema) {
    // A launch-param attribute name makes the schema open: the contract is
    // checked at runtime against the actual geometry.
    pgg::RunResult missing = runSrc(
        "param name: string = \"height2\"\n"
        "def probe(g: geo) -> (out: geo) {\n"
        "    \"\"\"Needs @height.\"\"\"\n"
        "    expect g has @height: \"нужен @height, см. make_heightfield\"\n"
        "    out = g\n"
        "}\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "tagged = set(base, name, 1.0)\n"
        "m = probe(tagged)\n"
        "output m\n");
    EXPECT_EQ(countCode(missing, "E303"), 1);
    EXPECT_TRUE(hasMessage(missing, "E303", "нужен @height"));
    EXPECT_TRUE(hasMessage(missing, "E303", "[instance probe[0]]"));
    // Written under the param name for real: the runtime check passes.
    pgg::RunResult present = runSrc(
        "param name: string = \"height\"\n"
        "def probe(g: geo) -> (out: geo) {\n"
        "    \"\"\"Needs @height.\"\"\"\n"
        "    expect g has @height: \"нужен @height, см. make_heightfield\"\n"
        "    out = g\n"
        "}\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "tagged = set(base, name, 1.0)\n"
        "m = probe(tagged)\n"
        "output m\n");
    pggtest::expectNoErrors(present);
}

TEST(Contract, ExpectConditionRuntimeE303) {
    pgg::RunResult r = runSrc(
        "def sized(size: f32) -> (out: f32) {\n"
        "    \"\"\"Positive size.\"\"\"\n"
        "    expect size > 0: \"размер в метрах, положительный\"\n"
        "    out = size\n"
        "}\n"
        "x = sized(-1.0)\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E303"), 1);
    EXPECT_TRUE(hasMessage(r, "E303", "размер в метрах"));
    // A satisfying call is clean.
    pgg::RunResult ok = runSrc(
        "def sized(size: f32) -> (out: f32) {\n"
        "    \"\"\"Positive size.\"\"\"\n"
        "    expect size > 0: \"размер в метрах, положительный\"\n"
        "    out = size\n"
        "}\n"
        "x = sized(1.0)\n"
        "output x\n");
    pggtest::expectNoErrors(ok);
}

TEST(Contract, ConstantConditionsDecideStatically) {
    // Literal-false: static E303 (before execution).
    pgg::RunResult r = runSrc(
        "def f(x: f32) -> (out: f32) {\n"
        "    \"\"\"F.\"\"\"\n"
        "    expect 0 > 1: \"never true\"\n"
        "    out = x\n"
        "}\n"
        "y = f(1.0)\n"
        "output y\n");
    EXPECT_EQ(countCode(r, "E303"), 1);
    EXPECT_TRUE(hasMessage(r, "E303", "never true"));
    EXPECT_TRUE(r.outputs.empty());
    // Literal-true: satisfied statically, runs clean.
    pgg::RunResult ok = runSrc(
        "def f(x: f32) -> (out: f32) {\n"
        "    \"\"\"F.\"\"\"\n"
        "    expect 1 > 0: \"never true\"\n"
        "    out = x\n"
        "}\n"
        "y = f(1.0)\n"
        "output y\n");
    pggtest::expectNoErrors(ok);
}

TEST(Contract, EnsureConditionRuntimeE304) {
    pgg::RunResult r = runSrc(
        "def prod(size: f32) -> (out: f32) {\n"
        "    \"\"\"Must produce a big value.\"\"\"\n"
        "    out = size\n"
        "    ensure out > 100: \"out must be big\"\n"
        "}\n"
        "x = prod(1.0)\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E304"), 1);
    EXPECT_TRUE(hasMessage(r, "E304", "out must be big"));
}

TEST(Contract, EnsureAttrFormStaticE304) {
    pgg::RunResult r = runSrc(
        "def tag(g: geo) -> (out: geo) {\n"
        "    \"\"\"Must write @wetness but does not.\"\"\"\n"
        "    out = g\n"
        "    ensure out has @wetness\n"
        "}\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = tag(base)\n"
        "output m\n");
    EXPECT_EQ(countCode(r, "E304"), 1);
    EXPECT_TRUE(hasMessage(r, "E304", "[instance tag[0]]"));
}

TEST(Contract, NestedInstanceChainInMessage) {
    // The chain reflects the nesting: outer[0].inner[0] (basic §9.5).
    pgg::RunResult r = runSrc(
        "def inner(size: f32) -> (out: f32) {\n"
        "    \"\"\"I.\"\"\"\n"
        "    expect size > 5: \"inner wants a big size\"\n"
        "    out = size\n"
        "}\n"
        "def outer(size: f32) -> (out: f32) {\n"
        "    \"\"\"O.\"\"\"\n"
        "    out = inner(size = size)\n"
        "}\n"
        "x = outer(1.0)\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E303"), 1);
    EXPECT_TRUE(hasMessage(r, "E303", "[instance outer[0].inner[0]]"));
}

TEST(Contract, RuntimeErrorInsideInstanceCarriesChain) {
    // A runtime failure on an open-schema input is tagged with the instance
    // path of the binding that raised it.
    pgg::RunResult r = runSrc(
        "param name: string = \"ab\"\n"
        "def explode(g: geo) -> (out: geo) {\n"
        "    \"\"\"Reads an attribute that is not there.\"\"\"\n"
        "    out = set_position(g, offset = @N * @missing_attr)\n"
        "}\n"
        "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "tagged = set(base, name, 1.0)\n"
        "m = explode(tagged)\n"
        "output m\n");
    EXPECT_EQ(countCode(r, "E302"), 1);
    EXPECT_TRUE(hasMessage(r, "E302", "[instance explode[0]]"));
}

}  // namespace
