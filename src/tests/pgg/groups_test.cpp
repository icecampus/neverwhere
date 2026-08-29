// §8.6 group tests: mark/unmark/ingroup round-trips, cross-domain reads with
// the > 0.5 threshold, groups in expressions, and runtime E305 for missing
// groups (distinct from E302 for missing attributes).
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

pgg::GeoPtr geoOutput(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return pgg::asGeo(o.value);
    return nullptr;
}

pgg::Value valueOutput(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return o.value;
    return pgg::Value();
}

TEST(Groups, MarkIngroupRoundTrip) {
    // Mark the top vertices of an icosphere; ingroup reads the same mask.
    pgg::RunResult r = pgg::run(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = mark(b, \"top\", where = dot(@N, (0, 1, 0)) > 0.8)\n"
        "c = count(m, where = ingroup(\"top\"))\n"
        "output m\n"
        "output c\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "m");
    ASSERT_TRUE(g);
    // Recompute the expected mask from the geometry itself.
    int expected = 0;
    for (const glm::vec3& n : *g->normals) expected += n.y > 0.8f ? 1 : 0;
    EXPECT_EQ(pgg::asInt(valueOutput(r, "c")), expected);
    // The group column lives on the points domain and matches.
    ASSERT_TRUE(g->pointGroups);
    pgg::ConstBoolColumnPtr col = g->pointGroups->find("top");
    ASSERT_TRUE(col);
    ASSERT_EQ(col->size(), g->pointCount());
    for (size_t i = 0; i < g->pointCount(); ++i)
        EXPECT_EQ((*col)[i], (*g->normals)[i].y > 0.8f ? 1 : 0) << i;
}

TEST(Groups, MarkOnFacesReadFromPoints) {
    // A faces-domain group read on points interpolates (> 0.5 threshold): a
    // point is in iff the majority of its incident faces is. Grid res 2, mark
    // faces whose centroid has x > 0 (the two right-hand quads).
    pgg::RunResult r = pgg::run(
        "b = grid(size = (4, 4), res = 2)\n"
        "m = mark(b, \"right\", where = dot(@P, (1, 0, 0)) > 0, domain = faces)\n"
        "c = count(m, where = ingroup(\"right\"))\n"
        "output c\n");
    expectNoErrors(r);
    // Right-column points (x = 2): all 3 in group. Middle points (x = 0): each
    // touches 2 faces, one right one left -> 0.5 is NOT > 0.5 -> out. Total 3.
    EXPECT_EQ(pgg::asInt(valueOutput(r, "c")), 3);
}

TEST(Groups, UnmarkRemovesTheGroup) {
    pgg::RunResult r = pgg::run(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = mark(b, \"g\", where = true)\n"
        "u = unmark(m, \"g\")\n"
        "output u\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "u");
    ASSERT_TRUE(g);
    EXPECT_FALSE(g->pointGroups && g->pointGroups->find("g"));

    // Reading after unmark is E305.
    pgg::RunResult bad = pgg::run(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = mark(b, \"g\", where = true)\n"
        "u = unmark(m, \"g\")\n"
        "c = count(u, where = ingroup(\"g\"))\n"
        "output c\n");
    EXPECT_EQ(countCode(bad, "E305"), 1);
}

TEST(Groups, MissingGroupIsRuntimeE305) {
    pgg::RunResult r = pgg::run(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "c = count(b, where = ingroup(\"nope\"))\n"
        "output c\n");
    EXPECT_EQ(countCode(r, "E305"), 1);
    // ... and not E302 (that is for attributes).
    EXPECT_EQ(countCode(r, "E302"), 0);
}

TEST(Groups, GroupCombinesInExpressions) {
    // Masks combine with & | ! as bool fields.
    pgg::RunResult r = pgg::run(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m1 = mark(b, \"top\", where = dot(@N, (0, 1, 0)) > 0.8)\n"
        "m2 = mark(m1, \"right\", where = dot(@N, (1, 0, 0)) > 0.8)\n"
        "c = count(m2, where = ingroup(\"top\") & !ingroup(\"right\"))\n"
        "output c\n");
    expectNoErrors(r);
    // Recompute from a fresh run of the marked geometry.
    pgg::RunResult m = pgg::run(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m2 = mark(mark(b, \"top\", where = dot(@N, (0, 1, 0)) > 0.8), \"right\", where = dot(@N, (1, 0, 0)) > 0.8)\n"
        "output m2\n");
    pgg::GeoPtr gm = geoOutput(m, "m2");
    int expected = 0;
    for (const glm::vec3& n : *gm->normals)
        if (n.y > 0.8f && n.x <= 0.8f) ++expected;
    EXPECT_EQ(pgg::asInt(valueOutput(r, "c")), expected);
}

TEST(Groups, DomainEnumValidation) {
    pgg::RunResult r = pgg::run(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = mark(b, \"g\", where = true, domain = bogus)\n"
        "output m\n");
    EXPECT_EQ(countCode(r, "E206"), 1);
}

TEST(Groups, MarkOnDetailBroadcasts) {
    pgg::RunResult r = pgg::run(
        "b = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "m = mark(b, \"all\", where = true, domain = detail)\n"
        "c = count(m, where = ingroup(\"all\"))\n"
        "output c\n");
    expectNoErrors(r);
    EXPECT_EQ(pgg::asInt(valueOutput(r, "c")), 42);
}

}  // namespace
