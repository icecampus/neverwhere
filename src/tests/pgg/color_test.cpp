// Surface color (§4.3 `@Cd`, §8.8 `@tint`): painting by material groups on the
// faces domain (the cottage.pgg idiom), per-instance @tint reaching the final
// color through realize, and the white neutral fill for @tint in merge — a
// tinted realized mesh next to plain geometry must not go black.
#include <gtest/gtest.h>

#include "pgg/eval.h"

namespace {

void expectNoErrors(const pgg::RunResult& r) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
    EXPECT_FALSE(r.hasErrors());
}

pgg::GeoPtr geoOutput(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return pgg::asGeo(o.value);
    return nullptr;
}

const std::vector<glm::vec3>* vec3Attr(const pgg::Geo& g, pgg::Domain d, const char* name) {
    const pgg::AttrSet* attrs = g.attrs(d);
    const pgg::AttrColumn* col = attrs ? attrs->find(name) : nullptr;
    if (!col) return nullptr;
    const auto* vec = std::get_if<std::shared_ptr<const std::vector<glm::vec3>>>(&col->data);
    return vec && *vec ? vec->get() : nullptr;
}

TEST(Color, PaintByGroupsOnFaces) {
    // Two boxes with material groups merged, then one set("Cd") over a
    // ternary chain on ingroup: the column lands on faces, no typeinfo needed
    // (Cd is reserved), every face gets its group's color.
    pgg::RunResult r = pgg::run(
        "a = mark(box(size = (1, 1, 1)), \"stone\", where = true, domain = faces)\n"
        "b = mark(transform(box(size = (1, 1, 1)), translate = (3, 0, 0)), \"wood\", where = true, domain = faces)\n"
        "g = merge(a, b)\n"
        "c1 = ingroup(\"stone\") ? vec3(0.5, 0.5, 0.5) : vec3(1, 0, 1)\n"
        "c2 = ingroup(\"wood\") ? vec3(0.4, 0.2, 0.1) : c1\n"
        "painted = set(g, \"Cd\", c2, domain = faces)\n"
        "output painted\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "painted");
    ASSERT_TRUE(g);
    const std::vector<glm::vec3>* cd = vec3Attr(*g, pgg::Domain::Faces, "Cd");
    ASSERT_TRUE(cd);
    ASSERT_EQ(cd->size(), g->faceCount());
    EXPECT_FALSE(vec3Attr(*g, pgg::Domain::Points, "Cd"));
    ASSERT_TRUE(g->faceGroups);
    pgg::ConstBoolColumnPtr stone = g->faceGroups->find("stone");
    ASSERT_TRUE(stone);
    for (size_t f = 0; f < g->faceCount(); ++f) {
        const glm::vec3 expected = (*stone)[f] ? glm::vec3(0.5f) : glm::vec3(0.4f, 0.2f, 0.1f);
        EXPECT_NEAR((*cd)[f].x, expected.x, 1e-6f) << f;
        EXPECT_NEAR((*cd)[f].y, expected.y, 1e-6f) << f;
        EXPECT_NEAR((*cd)[f].z, expected.z, 1e-6f) << f;
    }
}

TEST(Color, TintNeutralFillIsWhiteInMerge) {
    // realize always materializes point @tint; merging with a plain box must
    // fill the box's @tint with white (the multiplicative neutral and
    // realize's own default), not zeros — otherwise `base * @tint` paints the
    // plain part black.
    pgg::RunResult r = pgg::run(
        "pts0 = mesh_line(count = 2, length = 4.0, dir = (1, 0, 0))\n"
        "pts = set(pts0, \"tint\", (0.5, 0.5, 0.5))\n"
        "inst = realize(instance_on_points(pts, source = box(size = (1, 1, 1))))\n"
        "plain = transform(box(size = (1, 1, 1)), translate = (0, 5, 0))\n"
        "g = merge(inst, plain)\n"
        "output g\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "g");
    ASSERT_TRUE(g);
    const std::vector<glm::vec3>* tint = vec3Attr(*g, pgg::Domain::Points, "tint");
    ASSERT_TRUE(tint);
    ASSERT_EQ(tint->size(), g->pointCount());
    // Realized part first (2 boxes x 8 points), then the plain box.
    const size_t realizedPts = 16;
    for (size_t i = 0; i < realizedPts; ++i) EXPECT_NEAR((*tint)[i].x, 0.5f, 1e-6f) << i;
    for (size_t i = realizedPts; i < g->pointCount(); ++i) {
        EXPECT_NEAR((*tint)[i].x, 1.0f, 1e-6f) << i;
        EXPECT_NEAR((*tint)[i].y, 1.0f, 1e-6f) << i;
        EXPECT_NEAR((*tint)[i].z, 1.0f, 1e-6f) << i;
    }
    // Zero stays the neutral for everything else (a free f32 column).
    pgg::RunResult r2 = pgg::run(
        "a = set(box(size = (1, 1, 1)), \"shade\", 0.7, domain = points)\n"
        "g = merge(a, transform(box(size = (1, 1, 1)), translate = (0, 5, 0)))\n"
        "output g\n");
    expectNoErrors(r2);
    pgg::GeoPtr g2 = geoOutput(r2, "g");
    ASSERT_TRUE(g2 && g2->pointAttrs);
    const pgg::AttrColumn* shade = g2->pointAttrs->find("shade");
    ASSERT_TRUE(shade);
    const auto& col = *std::get<std::shared_ptr<const std::vector<float>>>(shade->data);
    EXPECT_NEAR(col[0], 0.7f, 1e-6f);
    EXPECT_NEAR(col[col.size() - 1], 0.0f, 1e-6f);
}

TEST(Color, TintThroughRealizeDrivesFaceColor) {
    // The cottage idiom: per-anchor @tint -> realize puts it on points ->
    // set("Cd", base * @tint, domain = faces) reads it resampled onto faces
    // (all points of one stamp share the tint, so the face value is exact).
    pgg::RunResult r = pgg::run(
        "pts0 = mesh_line(count = 2, length = 4.0, dir = (1, 0, 0))\n"
        "pts = set(pts0, \"tint\", @index == 0 ? vec3(0.5, 0.5, 0.5) : vec3(1, 1, 1))\n"
        "m = realize(instance_on_points(pts, source = box(size = (1, 1, 1))))\n"
        "painted = set(m, \"Cd\", vec3(0.8, 0.4, 0.2) * @tint, domain = faces)\n"
        "output painted\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "painted");
    ASSERT_TRUE(g);
    const std::vector<glm::vec3>* cd = vec3Attr(*g, pgg::Domain::Faces, "Cd");
    ASSERT_TRUE(cd);
    ASSERT_EQ(cd->size(), 12u);
    for (size_t f = 0; f < 6; ++f) EXPECT_NEAR((*cd)[f].x, 0.4f, 1e-5f) << f;
    for (size_t f = 6; f < 12; ++f) EXPECT_NEAR((*cd)[f].x, 0.8f, 1e-5f) << f;
}

}  // namespace
