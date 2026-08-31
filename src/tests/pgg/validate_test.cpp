// Validator-lite tests: E102 (SSA), E103 (forward ref), W001 (unused),
// unknown type names, W003 (one rng feeds 2+ stochastic nodes, §6.5).
#include <gtest/gtest.h>

#include <string>

#include "pgg/pgg.h"

namespace {

int countCode(const pgg::Document& doc, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : doc.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

TEST(Validate, CleanFileHasNoDiagnostics) {
    pgg::Document doc = pgg::parse(
        "a = fbm(seed = 1)\n"
        "b = a + 2\n"
        "output b\n");
    for (const pgg::Diagnostic& d : doc.diagnostics) ADD_FAILURE() << d.code << " " << d.message;
    EXPECT_TRUE(doc.diagnostics.empty());
}

TEST(Validate, ReassignmentIsE102) {
    pgg::Document doc = pgg::parse(
        "a = 1\n"
        "a = 2\n");
    EXPECT_EQ(countCode(doc, "E102"), 1);
}

TEST(Validate, ShadowingInZoneBodyIsE102) {
    pgg::Document doc = pgg::parse(
        "a = 1\n"
        "r = repeat (a, iterations = 2) |cur| {\n"
        "    a = 9\n"
        "    cur = a\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countCode(doc, "E102"), 1);
}

TEST(Validate, ZoneStatePortBoundOnceIsLegal) {
    pgg::Document doc = pgg::parse(
        "r = repeat (1, iterations = 2) |cur| {\n"
        "    cur = cur + 1\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countCode(doc, "E102"), 0);
    EXPECT_EQ(countCode(doc, "E103"), 0);
}

TEST(Validate, ZoneStatePortBoundTwiceIsE102) {
    pgg::Document doc = pgg::parse(
        "r = repeat (1, iterations = 2) |cur| {\n"
        "    cur = 1\n"
        "    cur = 2\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countCode(doc, "E102"), 1);
}

TEST(Validate, ForwardReferenceIsE103) {
    pgg::Document doc = pgg::parse(
        "a = b + 1\n"
        "b = 2\n"
        "output a\n");
    EXPECT_EQ(countCode(doc, "E103"), 1);
}

TEST(Validate, UseInsideZoneSeesOuterDefinitions) {
    pgg::Document doc = pgg::parse(
        "a = 1\n"
        "r = repeat (a, iterations = 2) |cur| {\n"
        "    cur = a + cur\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countCode(doc, "E103"), 0);
}

TEST(Validate, UnusedBindingIsW001) {
    pgg::Document doc = pgg::parse("unused_local = 5\n");
    EXPECT_EQ(countCode(doc, "W001"), 1);
    EXPECT_FALSE(doc.hasErrors());  // warning, not an error
}

TEST(Validate, OutputCountsAsUse) {
    pgg::Document doc = pgg::parse(
        "a = 1\n"
        "output a\n");
    EXPECT_EQ(countCode(doc, "W001"), 0);
}

TEST(Validate, DefParamsAreVisibleInBody) {
    pgg::Document doc = pgg::parse(
        "def f(x: geo) -> (o: geo) {\n"
        "    o = x\n"
        "}\n");
    EXPECT_EQ(countCode(doc, "E103"), 0);
    EXPECT_EQ(countCode(doc, "E102"), 0);
}

TEST(Validate, UnknownTypeNameIsE100) {
    pgg::Document doc = pgg::parse("param x: quux\noutput x\n");
    EXPECT_EQ(countCode(doc, "E100"), 1);
}

// --- W003: one rng feeds 2+ stochastic nodes (§6.5) -------------------------

TEST(Validate, SharedRngWarns) {
    pgg::Document doc = pgg::parse(
        "r = rng_from_seed(1)\n"
        "g = grid(size = (4, 4), res = 8)\n"
        "a = set_position(g, offset = fbm(scale = 2, rng = r) * 0.1)\n"
        "b = set_position(g, offset = vnoise(scale = 3, rng = r) * 0.1)\n"
        "output a\n"
        "output b\n");
    ASSERT_EQ(countCode(doc, "W003"), 1);
    for (const pgg::Diagnostic& d : doc.diagnostics) {
        if (d.code != "W003") continue;
        EXPECT_TRUE(d.isWarning);
        EXPECT_EQ(d.span.line, 4);  // the second consumer (b)
        EXPECT_NE(d.message.find("2 stochastic nodes"), std::string::npos) << d.message;
        EXPECT_NE(d.message.find("'a'"), std::string::npos) << d.message;
        EXPECT_NE(d.message.find("'b'"), std::string::npos) << d.message;
    }
}

TEST(Validate, ThreeConsumersOneWarning) {
    pgg::Document doc = pgg::parse(
        "r = rng_from_seed(1)\n"
        "g = grid(size = (4, 4), res = 8)\n"
        "a = set_position(g, offset = fbm(scale = 2, rng = r) * 0.1)\n"
        "b = set_position(g, offset = vnoise(scale = 3, rng = r) * 0.1)\n"
        "c = set_position(g, offset = fbm(scale = 4, rng = r) * 0.2)\n"
        "output c\n");
    ASSERT_EQ(countCode(doc, "W003"), 1);
    for (const pgg::Diagnostic& d : doc.diagnostics) {
        if (d.code != "W003") continue;
        EXPECT_NE(d.message.find("3 stochastic nodes"), std::string::npos) << d.message;
    }
}

TEST(Validate, SplitDifferentKeysNoWarn) {
    pgg::Document doc = pgg::parse(
        "r = rng_from_seed(1)\n"
        "a_rng = split_rng(r, key = \"a\")\n"
        "b_rng = split_rng(r, key = \"b\")\n"
        "g = grid(size = (4, 4), res = 8)\n"
        "x = set_position(g, offset = fbm(scale = 2, rng = a_rng) * 0.1)\n"
        "y = set_position(g, offset = vnoise(scale = 3, rng = b_rng) * 0.1)\n"
        "output x\n"
        "output y\n");
    EXPECT_EQ(countCode(doc, "W003"), 0);
}

TEST(Validate, SameSplitKeyWarns) {
    // Same parent + same constant key = the same subsequence, even through
    // two separate split bindings.
    pgg::Document doc = pgg::parse(
        "r = rng_from_seed(1)\n"
        "a_rng = split_rng(r, key = \"a\")\n"
        "b_rng = split_rng(r, key = \"a\")\n"
        "g = grid(size = (4, 4), res = 8)\n"
        "x = set_position(g, offset = fbm(scale = 2, rng = a_rng) * 0.1)\n"
        "y = set_position(g, offset = vnoise(scale = 3, rng = b_rng) * 0.1)\n"
        "output x\n"
        "output y\n");
    EXPECT_EQ(countCode(doc, "W003"), 1);
}

TEST(Validate, InlineSplitSameKeyWarns) {
    pgg::Document doc = pgg::parse(
        "r = rng_from_seed(1)\n"
        "g = grid(size = (4, 4), res = 8)\n"
        "x = set_position(g, offset = fbm(scale = 2, rng = split_rng(r, key = \"a\")) * 0.1)\n"
        "y = set_position(g, offset = vnoise(scale = 3, rng = split_rng(r, key = \"a\")) * 0.1)\n"
        "output x\n"
        "output y\n");
    EXPECT_EQ(countCode(doc, "W003"), 1);
}

TEST(Validate, AliasRngExempt) {
    // alias_rng declares the intentional repeat (§8.5): no warning at any
    // depth of the chain (binding, inline, alias of an alias).
    pgg::Document doc = pgg::parse(
        "r = rng_from_seed(1)\n"
        "a_rng = alias_rng(r)\n"
        "b_rng = alias_rng(a_rng)\n"
        "g = grid(size = (4, 4), res = 8)\n"
        "x = set_position(g, offset = fbm(scale = 2, rng = a_rng) * 0.1)\n"
        "y = set_position(g, offset = vnoise(scale = 3, rng = b_rng) * 0.1)\n"
        "z = set_position(g, offset = fbm(scale = 4, rng = alias_rng(r)) * 0.2)\n"
        "output x\n"
        "output y\n"
        "output z\n");
    EXPECT_EQ(countCode(doc, "W003"), 0);
}

TEST(Validate, NonConstantSplitKeySharedWarns) {
    // One split node keyed by @piece_index = one subsequence: feeding it to
    // two stochastic nodes correlates within the piece.
    pgg::Document doc = pgg::parse(
        "root = rng_from_seed(1)\n"
        "parts = split_rng(root, key = \"parts\")\n"
        "b = box(size = (1, 1, 1))\n"
        "f = foreach piece in b {\n"
        "    piece_rng = split_rng(parts, key = @piece_index)\n"
        "    j = random(lo = 0.0, hi = 1.0, rng = piece_rng, counter = 0)\n"
        "    k = random_int(n = 2, rng = piece_rng)\n"
        "    piece = set_position(piece, offset = vec3(j, f32(k), 0.0))\n"
        "}\n"
        "output f\n");
    EXPECT_EQ(countCode(doc, "W003"), 1);
}

TEST(Validate, DefParamSharedWarns) {
    pgg::Document doc = pgg::parse(
        "def displace_twice(g: geo, rng: rng) -> (out: geo) {\n"
        "    \"\"\"Two noise passes on one generator.\"\"\"\n"
        "    a = set_position(g, offset = fbm(scale = 2, rng = rng) * 0.1)\n"
        "    out = set_position(a, offset = vnoise(scale = 3, rng = rng) * 0.1)\n"
        "}\n"
        "root = rng_from_seed(1)\n"
        "g0 = grid(size = (4, 4), res = 8)\n"
        "r2 = displace_twice(g = g0, rng = root)\n"
        "output r2\n");
    EXPECT_EQ(countCode(doc, "W003"), 1);
}

TEST(Validate, SingleConsumerNoWarn) {
    pgg::Document doc = pgg::parse(
        "r = rng_from_seed(1)\n"
        "n_rng = split_rng(r, key = \"n\")\n"
        "g = grid(size = (4, 4), res = 8)\n"
        "a = set_position(g, offset = fbm(scale = 2, rng = n_rng) * 0.1)\n"
        "output a\n");
    EXPECT_EQ(countCode(doc, "W003"), 0);
}

TEST(Validate, MissingRngNoCrash) {
    // A stochastic op without an rng argument is E202 at typecheck, not a
    // lint topic: no consumer is recorded and nothing is resolved.
    pgg::Document doc = pgg::parse(
        "g = grid(size = (4, 4), res = 8)\n"
        "a = set_position(g, offset = fbm(scale = 2) * 0.1)\n"
        "output a\n");
    EXPECT_EQ(countCode(doc, "W003"), 0);
}

TEST(Validate, DefCallSharingOutOfScope) {
    // One rng on two def instances is the intentional-repeat idiom of §7.3
    // (bit-identical results, pinned by Def.SameRngReproducesSameResult) —
    // W003 covers built-in stochastic ops only.
    pgg::Document doc = pgg::parse(
        "def cloud(n: int, rng: rng) -> (out: geo<points>) {\n"
        "    \"\"\"Uniform points in a unit box.\"\"\"\n"
        "    out = point_cloud(count = n, bounds = (1, 1, 1), rng = rng)\n"
        "}\n"
        "root = rng_from_seed(7)\n"
        "a = cloud(n = 4, rng = root)\n"
        "b = cloud(n = 4, rng = root)\n"
        "output a\n"
        "output b\n");
    EXPECT_EQ(countCode(doc, "W003"), 0);
}

TEST(Validate, CorpusHasNoW003) {
    // False-positive barrier: the acceptance etalons split/alias their
    // generators deliberately and must stay clean.
    for (const char* name : {"tower.pgg", "e1_rock.pgg", "e2_rock_scatter.pgg", "e4_sdf_rock.pgg",
                             "e7_repeat_settle.pgg", "e7_fracture.pgg"}) {
        pgg::Document doc = pgg::parseFile(std::string(PGG_CORPUS_DIR) + "/" + name);
        ASSERT_FALSE(doc.hasErrors()) << name;
        EXPECT_EQ(countCode(doc, "W003"), 0) << name;
    }
}

}  // namespace
