// Validator-lite tests: E102 (SSA), E103 (forward ref), W001 (unused),
// unknown type names.
#include <gtest/gtest.h>

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

}  // namespace
