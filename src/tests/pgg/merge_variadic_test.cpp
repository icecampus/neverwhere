// §8.3 merge(a, b, c, ...): variadic left fold. Order, equivalence with the
// nested form, group/attr propagation, static schema across the tail,
// E609 caught in the tail, non-geo operands E204, arity still >= 2.
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/src/eval/geometry.h"
#include "test_utils.h"

namespace {

int countCode(const pgg::RunResult& r, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

pgg::GeoPtr geoOutput(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return pgg::asGeo(o.value);
    return nullptr;
}

const char* kFour =
    "a = box(size = (1, 1, 1))\n"
    "b = transform(box(size = (1, 1, 1)), translate = (2, 0, 0))\n"
    "c = transform(box(size = (1, 1, 1)), translate = (4, 0, 0))\n"
    "d = mark(transform(box(size = (1, 1, 1)), translate = (6, 0, 0)), \"g\", where = true, domain = faces)\n";

TEST(MergeVariadic, FlatEqualsNestedBitForBit) {
    pgg::RunResult flat = pgg::run(std::string(kFour) + "m = merge(a, b, c, d)\noutput m\n");
    pgg::RunResult nested = pgg::run(std::string(kFour) + "m = merge(merge(a, b), merge(c, d))\noutput m\n");
    ASSERT_FALSE(flat.hasErrors());
    ASSERT_FALSE(nested.hasErrors());
    pgg::GeoPtr f = geoOutput(flat, "m"), n = geoOutput(nested, "m");
    ASSERT_TRUE(f && n);
    EXPECT_EQ(f->pointCount(), 32u);
    EXPECT_EQ(f->faceCount(), 24u);
    EXPECT_EQ(pggtest::geoContentHash(*f), pggtest::geoContentHash(*n));
    // Group from the last operand covers only its box: count() runs on points,
    // the faces group is read across domains (> 0.5) -> the 8 points of d;
    // the static schema knows the group (no E305 on the read).
    pgg::RunResult g = pgg::run(std::string(kFour) + "m = merge(a, b, c, d)\nn = count(m, where = ingroup(\"g\"))\noutput n\n");
    ASSERT_FALSE(g.hasErrors());
    for (const auto& o : g.outputs)
        if (o.name == "n") EXPECT_EQ(pgg::asInt(o.value), 8);
}

TEST(MergeVariadic, TailConflictIsE609Statically) {
    // @h on points in a, on faces in c: the conflict sits in the variadic tail.
    pgg::RunResult r = pgg::run(
        "a = set(box(size = (1, 1, 1)), \"h\", 1.0, domain = points)\n"
        "b = box(size = (1, 1, 1))\n"
        "c = set(box(size = (1, 1, 1)), \"h\", 2.0, domain = faces)\n"
        "m = merge(a, b, c)\n"
        "output m\n");
    EXPECT_GE(countCode(r, "E609"), 1);
}

TEST(MergeVariadic, NonGeoOperandIsE204AndArityStaysTwo) {
    pgg::RunResult r = pgg::run("a = box(size = (1, 1, 1))\nm = merge(a, a, 3)\noutput m\n");
    EXPECT_GE(countCode(r, "E204"), 1);
    pgg::RunResult r2 = pgg::run("a = box(size = (1, 1, 1))\nm = merge(a)\noutput m\n");
    EXPECT_GE(countCode(r2, "E202"), 1);
}

}  // namespace
