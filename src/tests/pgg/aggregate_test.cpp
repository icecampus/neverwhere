// §8.10 aggregator tests: bbox as the first multi-output node (destructuring
// semantics and its arity/type errors), extent/centroid/count, and the
// min_of/max_of/avg_of/sum_of reductions strictly in @index order with the
// E601 empty-selection rule.
#include <gtest/gtest.h>

#include "pgg/eval.h"

namespace {

void expectNoErrors(const pgg::RunResult& r) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
    EXPECT_FALSE(r.hasErrors());
}

int countCode(const pgg::RunResult& r, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

pgg::Value valueOutput(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return o.value;
    return pgg::Value();
}

TEST(Aggregate, BboxDestructure) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (2, 4, 6))\n"
        "mn, mx = bbox(b)\n"
        "output mn\n"
        "output mx\n");
    expectNoErrors(r);
    EXPECT_EQ(pgg::asVec3(valueOutput(r, "mn")), glm::vec3(-1, -2, -3));
    EXPECT_EQ(pgg::asVec3(valueOutput(r, "mx")), glm::vec3(1, 2, 3));
}

TEST(Aggregate, BboxDestructuredTargetsAreTypedAndReusable) {
    // The destructured names are ordinary vec3 bindings downstream.
    pgg::RunResult r = pgg::run(
        "b = box(size = (2, 4, 6))\n"
        "mn, mx = bbox(b)\n"
        "size = mx - mn\n"
        "output size\n");
    expectNoErrors(r);
    EXPECT_EQ(pgg::asVec3(valueOutput(r, "size")), glm::vec3(2, 4, 6));
}

TEST(Aggregate, BboxArityMismatchIsE202) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (2, 4, 6))\n"
        "a, b2, c = bbox(b)\n"
        "output a\n");
    EXPECT_EQ(countCode(r, "E202"), 1);
}

TEST(Aggregate, BboxSingleTargetIsE204) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (2, 4, 6))\n"
        "x = bbox(b)\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E204"), 1);
}

TEST(Aggregate, DestructureOfSingleResultIsE202) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (2, 4, 6))\n"
        "a, b2 = extent(b)\n"
        "output a\n");
    EXPECT_EQ(countCode(r, "E202"), 1);
}

TEST(Aggregate, ExtentAndCentroid) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (2, 4, 6))\n"
        "t = transform(b, translate = (5, 0, 0))\n"
        "e = extent(t)\n"
        "c = centroid(t)\n"
        "output e\n"
        "output c\n");
    expectNoErrors(r);
    EXPECT_EQ(pgg::asVec3(valueOutput(r, "e")), glm::vec3(2, 4, 6));
    const glm::vec3 c = pgg::asVec3(valueOutput(r, "c"));
    EXPECT_NEAR(c.x, 5.0f, 1e-6f);
    EXPECT_NEAR(c.y, 0.0f, 1e-6f);
    EXPECT_NEAR(c.z, 0.0f, 1e-6f);
}

TEST(Aggregate, CountDomainsAndMask) {
    pgg::RunResult r = pgg::run(
        "b = grid(size = (4, 4), res = 2)\n"
        "p = count(b)\n"
        "f = count(b, domain = faces)\n"
        "c = count(b, domain = corners)\n"
        "d = count(b, domain = detail)\n"
        "right = count(b, where = dot(@P, (1, 0, 0)) > 0)\n"
        "output p\n"
        "output f\n"
        "output c\n"
        "output d\n"
        "output right\n");
    expectNoErrors(r);
    EXPECT_EQ(pgg::asInt(valueOutput(r, "p")), 9);
    EXPECT_EQ(pgg::asInt(valueOutput(r, "f")), 4);
    EXPECT_EQ(pgg::asInt(valueOutput(r, "c")), 16);
    EXPECT_EQ(pgg::asInt(valueOutput(r, "d")), 1);
    EXPECT_EQ(pgg::asInt(valueOutput(r, "right")), 3);
}

TEST(Aggregate, ReductionsInIndexOrder) {
    // mesh_line(count = 5): @index is 0..4 — sum 10, avg 2, min 0, max 4.
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 5, length = 4.0)\n"
        "s = sum_of(@index, on = l)\n"
        "a = avg_of(@index, on = l)\n"
        "lo = min_of(@index, on = l)\n"
        "hi = max_of(@index, on = l)\n"
        "output s\n"
        "output a\n"
        "output lo\n"
        "output hi\n");
    expectNoErrors(r);
    EXPECT_FLOAT_EQ(pgg::asF32(valueOutput(r, "s")), 10.0f);
    EXPECT_FLOAT_EQ(pgg::asF32(valueOutput(r, "a")), 2.0f);
    EXPECT_FLOAT_EQ(pgg::asF32(valueOutput(r, "lo")), 0.0f);
    EXPECT_FLOAT_EQ(pgg::asF32(valueOutput(r, "hi")), 4.0f);
}

TEST(Aggregate, ReductionWithMask) {
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 5, length = 4.0)\n"
        "s = sum_of(@index, on = l, where = @index >= 3)\n"
        "output s\n");
    expectNoErrors(r);
    EXPECT_FLOAT_EQ(pgg::asF32(valueOutput(r, "s")), 7.0f);  // 3 + 4
}

TEST(Aggregate, EmptySelectionRules) {
    // avg/min/max over an empty selection are E601; count is 0, sum is 0.
    pgg::RunResult avg = pgg::run(
        "l = mesh_line(count = 5, length = 4.0)\n"
        "x = avg_of(@index, on = l, where = @index > 100)\n"
        "output x\n");
    EXPECT_EQ(countCode(avg, "E601"), 1);
    pgg::RunResult mn = pgg::run(
        "l = mesh_line(count = 5, length = 4.0)\n"
        "x = min_of(@index, on = l, where = @index > 100)\n"
        "output x\n");
    EXPECT_EQ(countCode(mn, "E601"), 1);
    pgg::RunResult mx = pgg::run(
        "l = mesh_line(count = 5, length = 4.0)\n"
        "x = max_of(@index, on = l, where = @index > 100)\n"
        "output x\n");
    EXPECT_EQ(countCode(mx, "E601"), 1);
    pgg::RunResult cnt = pgg::run(
        "l = mesh_line(count = 5, length = 4.0)\n"
        "x = count(l, where = @index > 100)\n"
        "output x\n");
    expectNoErrors(cnt);
    EXPECT_EQ(pgg::asInt(valueOutput(cnt, "x")), 0);
    pgg::RunResult sum = pgg::run(
        "l = mesh_line(count = 5, length = 4.0)\n"
        "x = sum_of(@index, on = l, where = @index > 100)\n"
        "output x\n");
    expectNoErrors(sum);
    EXPECT_FLOAT_EQ(pgg::asF32(valueOutput(sum, "x")), 0.0f);
}

}  // namespace
