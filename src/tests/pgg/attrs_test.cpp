// §8.7 attribute tests: set (domain inference, materialized values),
// remove_attr/rename_attr, promote (sum/average/first across domains), the
// §4.3 silent interpolation matrix, and runtime E302 for unknown attributes.
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

template <typename T>
std::shared_ptr<const std::vector<T>> columnOf(const pgg::Geo& geo, pgg::Domain domain,
                                               const std::string& name) {
    const pgg::AttrSet* attrs = geo.attrs(domain);
    const pgg::AttrColumn* col = attrs ? attrs->find(name) : nullptr;
    if (!col) return nullptr;
    return std::get<std::shared_ptr<const std::vector<T>>>(col->data);
}

// grid(size = (4, 4), res = 2): 3x3 points at x/z in {-2, 0, 2}, 4 quad faces.
const char* kGrid = "b = grid(size = (4, 4), res = 2)\n";

TEST(Attrs, SetConstantFieldLandsOnDetail) {
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s = set(b, \"k\", 3.5)\n"
                                "output s\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "s");
    ASSERT_TRUE(g);
    EXPECT_TRUE(g->detailAttrs && g->detailAttrs->find("k"));
    EXPECT_FALSE(g->pointAttrs && g->pointAttrs->find("k"));
    auto col = columnOf<float>(*g, pgg::Domain::Detail, "k");
    ASSERT_TRUE(col);
    ASSERT_EQ(col->size(), 1u);
    EXPECT_FLOAT_EQ((*col)[0], 3.5f);
}

TEST(Attrs, SetFieldLandsOnPointsAndMaterializes) {
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s = set(b, \"x\", dot(@P, (1, 0, 0)))\n"
                                "output s\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "s");
    ASSERT_TRUE(g);
    auto col = columnOf<float>(*g, pgg::Domain::Points, "x");
    ASSERT_TRUE(col);
    ASSERT_EQ(col->size(), g->pointCount());
    // The column is the materialized field: x-coordinate of every point.
    for (size_t i = 0; i < g->pointCount(); ++i) EXPECT_FLOAT_EQ((*col)[i], (*g->positions)[i].x);
}

TEST(Attrs, ReadBackAndTypes) {
    // int and vec3 attributes keep their types; bool fields store u8.
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s1 = set(b, \"i\", @index)\n"
                                "s2 = set(s1, \"v\", @P)\n"
                                "s = set(s2, \"m\", dot(@P, (1, 0, 0)) > 0)\n"
                                "output s\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "s");
    ASSERT_TRUE(g);
    auto icol = columnOf<int64_t>(*g, pgg::Domain::Points, "i");
    auto vcol = columnOf<glm::vec3>(*g, pgg::Domain::Points, "v");
    auto mcol = columnOf<uint8_t>(*g, pgg::Domain::Points, "m");
    ASSERT_TRUE(icol);
    ASSERT_TRUE(vcol);
    ASSERT_TRUE(mcol);
    for (size_t i = 0; i < g->pointCount(); ++i) {
        EXPECT_EQ((*icol)[i], static_cast<int64_t>(i));
        EXPECT_EQ((*vcol)[i], (*g->positions)[i]);
        EXPECT_EQ((*mcol)[i], (*g->positions)[i].x > 0 ? 1 : 0);
    }
}

TEST(Attrs, RemoveAndRename) {
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s = set(b, \"x\", dot(@P, (1, 0, 0)))\n"
                                "t = rename_attr(s, \"x\", \"y\")\n"
                                "u = remove_attr(t, \"y\")\n"
                                "output t\n"
                                "output u\n");
    expectNoErrors(r);
    pgg::GeoPtr t = geoOutput(r, "t");
    pgg::GeoPtr u = geoOutput(r, "u");
    ASSERT_TRUE(t);
    ASSERT_TRUE(u);
    EXPECT_TRUE(columnOf<float>(*t, pgg::Domain::Points, "y"));
    EXPECT_FALSE(columnOf<float>(*t, pgg::Domain::Points, "x"));
    EXPECT_FALSE(columnOf<float>(*u, pgg::Domain::Points, "y"));

    // Reading a removed attribute is a runtime E302.
    pgg::RunResult bad = pgg::run(std::string(kGrid) +
                                  "s = set(b, \"x\", 1.0)\n"
                                  "t = remove_attr(s, \"x\")\n"
                                  "c = sum_of(@x, on = t)\n"
                                  "output c\n");
    EXPECT_EQ(countCode(bad, "E302"), 1);
}

TEST(Attrs, PromoteModesPointsToFaces) {
    // Face values of promote("x", points -> faces): avg/sum/first over the 4
    // corner points. Face 0 of the grid spans x in {-2, 0} -> avg -1, sum -4,
    // first corner's point is (-2, -2) -> -2.
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s = set(b, \"x\", dot(@P, (1, 0, 0)))\n"
                                "pa = promote(s, \"x\", from = points, to = faces, mode = average)\n"
                                "ps = promote(s, \"x\", from = points, to = faces, mode = sum)\n"
                                "pf = promote(s, \"x\", from = points, to = faces, mode = first)\n"
                                "output pa\n"
                                "output ps\n"
                                "output pf\n");
    expectNoErrors(r);
    auto avg = columnOf<float>(*geoOutput(r, "pa"), pgg::Domain::Faces, "x");
    auto sum = columnOf<float>(*geoOutput(r, "ps"), pgg::Domain::Faces, "x");
    auto first = columnOf<float>(*geoOutput(r, "pf"), pgg::Domain::Faces, "x");
    ASSERT_TRUE(avg);
    ASSERT_TRUE(sum);
    ASSERT_TRUE(first);
    ASSERT_EQ(avg->size(), 4u);
    EXPECT_FLOAT_EQ((*avg)[0], -1.0f);
    EXPECT_FLOAT_EQ((*sum)[0], -4.0f);
    EXPECT_FLOAT_EQ((*first)[0], -2.0f);
    // Face averages of the four quads: x in {-1, 1} column-wise.
    EXPECT_FLOAT_EQ((*avg)[1], 1.0f);
    EXPECT_FLOAT_EQ((*avg)[2], -1.0f);
    EXPECT_FLOAT_EQ((*avg)[3], 1.0f);
}

TEST(Attrs, PromoteFacesToPointsAndDetail) {
    // faces -> points averages incident face values; * -> detail averages all.
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s = set(b, \"x\", dot(@P, (1, 0, 0)))\n"
                                "pf = promote(s, \"x\", from = points, to = faces, mode = average)\n"
                                "pp = promote(pf, \"x\", from = faces, to = points, mode = sum)\n"
                                "pd = promote(pf, \"x\", from = faces, to = detail, mode = average)\n"
                                "output pp\n"
                                "output pd\n");
    expectNoErrors(r);
    auto pts = columnOf<float>(*geoOutput(r, "pp"), pgg::Domain::Points, "x");
    auto det = columnOf<float>(*geoOutput(r, "pd"), pgg::Domain::Detail, "x");
    ASSERT_TRUE(pts);
    ASSERT_TRUE(det);
    // Corner point (0,0) of the grid touches one face with avg -1; the center
    // point touches all four faces: sum of averages (-1+1-1+1) = 0.
    EXPECT_FLOAT_EQ((*pts)[0], -1.0f);
    EXPECT_FLOAT_EQ((*pts)[4], 0.0f);
    // detail = average of face averages = 0 (symmetric).
    EXPECT_FLOAT_EQ((*det)[0], 0.0f);
}

TEST(Attrs, PromoteMissingAttributeIsE302) {
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "t = promote(b, \"nope\", from = points, to = faces)\n"
                                "output t\n");
    EXPECT_EQ(countCode(r, "E302"), 1);
}

TEST(Attrs, SilentInterpolationReadOnFaces) {
    // No promote: @x (points) read on the faces domain averages corner points.
    // Two of the four quads have average x > 0.
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s = set(b, \"x\", dot(@P, (1, 0, 0)))\n"
                                "c = count(s, domain = faces, where = @x > 0)\n"
                                "output c\n");
    expectNoErrors(r);
    EXPECT_EQ(pgg::asInt(valueOutput(r, "c")), 2);
}

TEST(Attrs, SilentInterpolationCornersAndDetail) {
    // Corners copy their point's value: 4 corners have x == 2 (one corner at
    // each of (2,-2) and (2,2), two at (2,0)).
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s = set(b, \"x\", dot(@P, (1, 0, 0)))\n"
                                "c = count(s, domain = corners, where = @x > 1)\n"
                                "d = count(s, domain = detail, where = @x > -1)\n"
                                "output c\n"
                                "output d\n");
    expectNoErrors(r);
    EXPECT_EQ(pgg::asInt(valueOutput(r, "c")), 4);
    // detail read of a points attribute is the average (0 here) -> true -> 1.
    EXPECT_EQ(pgg::asInt(valueOutput(r, "d")), 1);
}

TEST(Attrs, DetailBroadcastOnRead) {
    // A detail attribute read on points broadcasts to every element.
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s = set(b, \"k\", 2.0)\n"
                                "c = sum_of(@k, on = s)\n"
                                "output c\n");
    expectNoErrors(r);
    EXPECT_FLOAT_EQ(pgg::asF32(valueOutput(r, "c")), 18.0f);  // 9 points * 2.0
}

TEST(Attrs, UnknownAttributeReadIsRuntimeE302) {
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "c = avg_of(@nope, on = b)\n"
                                "output c\n");
    EXPECT_EQ(countCode(r, "E302"), 1);
}

TEST(Attrs, SetReservedNameIsE301) {
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s = set(b, \"P\", 1.0)\n"
                                "output s\n");
    EXPECT_EQ(countCode(r, "E301"), 1);
}

TEST(Attrs, SetReplacesSameNameOnOtherDomains) {
    // §8.7: the attribute name is global — a later set() replaces the column on
    // every domain (last write wins), so no stale copy can shadow the new value
    // on the read path (the reader takes the first hit in points/.../detail).
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s1 = set(b, \"k\", dot(@P, (1, 0, 0)))\n"
                                "s2 = set(s1, \"k\", 7.5)\n"
                                "c = sum_of(@k, on = s2)\n"
                                "output s2\n"
                                "output c\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "s2");
    ASSERT_TRUE(g);
    EXPECT_FALSE(g->pointAttrs && g->pointAttrs->find("k"));
    auto det = columnOf<float>(*g, pgg::Domain::Detail, "k");
    ASSERT_TRUE(det);
    EXPECT_FLOAT_EQ((*det)[0], 7.5f);
    // Expression reads on points see the constant (broadcast), not the stale column.
    EXPECT_FLOAT_EQ(pgg::asF32(valueOutput(r, "c")), 67.5f);
}

TEST(Attrs, SetFieldReplacesStaleDetailColumn) {
    pgg::RunResult r = pgg::run(std::string(kGrid) +
                                "s1 = set(b, \"k\", 7.5)\n"
                                "s2 = set(s1, \"k\", dot(@P, (1, 0, 0)))\n"
                                "output s2\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "s2");
    ASSERT_TRUE(g);
    EXPECT_FALSE(g->detailAttrs && g->detailAttrs->find("k"));
    auto pts = columnOf<float>(*g, pgg::Domain::Points, "k");
    ASSERT_TRUE(pts);
    ASSERT_EQ(pts->size(), g->pointCount());
    EXPECT_FLOAT_EQ((*pts)[0], -2.0f);
}

TEST(Attrs, ConstantOrientOnAnchorIsHonoredByInstancer) {
    // Regression (stone_arch): an anchor that already carries a points-domain
    // @orient must honor a later CONSTANT set() — the constant used to land on
    // detail and be shadowed by the stale points column, so the roll below was
    // silently dropped by instance_on_points.
    pgg::RunResult r = pgg::run(
        "r = rng_from_seed(1)\n"
        "a0 = set(mesh_line(count = 1, length = 0.0), \"orient\", orient_from_euler(random_vec(lo = (0, 10, 0), hi = (0, 10, 0), rng = r)))\n"
        "a = set(a0, \"orient\", orient_from_euler((0, 0, 90)))\n"
        "m = realize(instance_on_points(a, source = box(size = (2, 0.2, 0.2))))\n"
        "output m\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "m");
    ASSERT_TRUE(g);
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    const glm::vec3 ext = mx - mn;
    // Roll 90° around Z maps the long X axis of the box onto Y.
    EXPECT_NEAR(ext.x, 0.2f, 1e-4f);
    EXPECT_NEAR(ext.y, 2.0f, 1e-4f);
    EXPECT_NEAR(ext.z, 0.2f, 1e-4f);
}

TEST(Attrs, ConstantScaleOnAnchorIsHonoredByInstancer) {
    // Same shadowing trap through @scale: a constant scale set over an
    // inherited points-domain scale must win (doubling the box).
    pgg::RunResult r = pgg::run(
        "r = rng_from_seed(1)\n"
        "a0 = set(mesh_line(count = 1, length = 0.0), \"scale\", random(lo = 0.5, hi = 0.5, rng = r))\n"
        "a = set(a0, \"scale\", 2.0)\n"
        "m = realize(instance_on_points(a, source = box(size = (1, 1, 1))))\n"
        "output m\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "m");
    ASSERT_TRUE(g);
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    EXPECT_NEAR((mx - mn).x, 2.0f, 1e-4f);
}

}  // namespace
