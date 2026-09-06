// §8.3 clip: half-space clip with capped cut loops. Counts and watertightness
// on a box, kept side = normal direction, welded cut edge, attribute/group
// interpolation, cap inheritance + cap_group, identity/empty extremes, points
// geometry, E204 on instances, E612 on a zero normal.
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/src/eval/geometry.h"

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

const std::vector<glm::vec3>* vec3Attr(const pgg::Geo& g, pgg::Domain d, const char* name) {
    const pgg::AttrSet* attrs = g.attrs(d);
    const pgg::AttrColumn* col = attrs ? attrs->find(name) : nullptr;
    if (!col) return nullptr;
    const auto* vec = std::get_if<std::shared_ptr<const std::vector<glm::vec3>>>(&col->data);
    return vec && *vec ? vec->get() : nullptr;
}

TEST(Clip, BoxHalfIsWatertightWithCap) {
    // 2x1x1 box cut at x = 0.5 keeping -x: one face dropped, one kept whole,
    // four cut into quads, one square cap -> still 8 points / 6 faces.
    pgg::RunResult r = pgg::run(
        "b = mark(box(size = (2, 1, 1)), \"stone\", where = true, domain = faces)\n"
        "c = clip(b, origin = (0.5, 0, 0), normal = (-1, 0, 0), cap_group = \"cut\")\n"
        "output c\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "c");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->pointCount(), 8u);
    EXPECT_EQ(g->faceCount(), 6u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    EXPECT_NEAR(mn.x, -1.0f, 1e-6f);
    EXPECT_NEAR(mx.x, 0.5f, 1e-6f);
    // Cap: outward (+x) normal, in "cut" and (inherited) in "stone".
    ASSERT_TRUE(g->faceGroups);
    pgg::ConstBoolColumnPtr cut = g->faceGroups->find("cut");
    pgg::ConstBoolColumnPtr stone = g->faceGroups->find("stone");
    ASSERT_TRUE(cut && stone);
    int caps = 0;
    for (size_t f = 0; f < g->faceCount(); ++f) {
        EXPECT_EQ((*stone)[f], 1) << f;
        if (!(*cut)[f]) continue;
        caps += 1;
        const glm::vec3 n = glm::normalize(pgg::faceNormal(*g, f));
        EXPECT_NEAR(n.x, 1.0f, 1e-5f);
    }
    EXPECT_EQ(caps, 1);
    // Every face normal points away from the centroid (consistent winding).
    const glm::vec3 center(-0.25f, 0.0f, 0.0f);
    for (size_t f = 0; f < g->faceCount(); ++f) {
        glm::vec3 fc(0.0f);
        const int32_t b = (*g->faceOffsets)[f], e = (*g->faceOffsets)[f + 1];
        for (int32_t c = b; c < e; ++c) fc += (*g->positions)[static_cast<size_t>((*g->cornerVerts)[static_cast<size_t>(c)])];
        fc /= static_cast<float>(e - b);
        EXPECT_GT(glm::dot(pgg::faceNormal(*g, f), fc - center), 0.0f) << f;
    }
}

TEST(Clip, KeepsTheSideTheNormalPointsInto) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (2, 1, 1))\n"
        "c = clip(b, origin = (0.5, 0, 0), normal = (1, 0, 0))\n"
        "output c\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "c");
    ASSERT_TRUE(g);
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    EXPECT_NEAR(mn.x, 0.5f, 1e-6f);
    EXPECT_NEAR(mx.x, 1.0f, 1e-6f);
    EXPECT_EQ(g->faceCount(), 6u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
}

TEST(Clip, ObliqueCutInterpolatesColumns) {
    // Diagonal plane through a box: cut points are shared (welded), point
    // attributes lerp along the edges, corner N of the cap is -normal,
    // point @N marks the schema stale (W006 on a later read).
    pgg::RunResult r = pgg::run(
        "b0 = compute_normals(box(size = (2, 2, 2)), mode = flat)\n"
        "b = set(b0, \"h\", dot(@P, (1, 0, 0)), domain = points)\n"
        "c = clip(b, origin = (0, 0, 0), normal = (1, 1, 0))\n"
        "n = count(c, where = dot(@N, (1, 0, 0)) > 2)\n"
        "output c\n"
        "output n\n");
    EXPECT_FALSE(r.hasErrors());
    EXPECT_EQ(countCode(r, "W006"), 1);
    pgg::GeoPtr g = geoOutput(r, "c");
    ASSERT_TRUE(g);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    const glm::vec3 nrm = glm::normalize(glm::vec3(1, 1, 0));
    for (const glm::vec3& p : *g->positions) EXPECT_GE(glm::dot(p, nrm), -1e-5f);
    // Point attribute h == x everywhere, including the new cut points.
    const pgg::AttrColumn* h = g->pointAttrs->find("h");
    ASSERT_TRUE(h);
    const auto& hv = *std::get<std::shared_ptr<const std::vector<float>>>(h->data);
    for (size_t i = 0; i < g->pointCount(); ++i) EXPECT_NEAR(hv[i], (*g->positions)[i].x, 1e-5f) << i;
    // Cap corners: flat normal = -normal; the cap lies in the plane.
    const std::vector<glm::vec3>* cn = vec3Attr(*g, pgg::Domain::Corners, "N");
    ASSERT_TRUE(cn);
    int capFaces = 0;
    for (size_t f = 0; f < g->faceCount(); ++f) {
        const glm::vec3 fn = glm::normalize(pgg::faceNormal(*g, f));
        if (glm::dot(fn, -nrm) < 0.999f) continue;
        capFaces += 1;
        for (int32_t c = (*g->faceOffsets)[f]; c < (*g->faceOffsets)[f + 1]; ++c) {
            EXPECT_NEAR(glm::dot((*cn)[static_cast<size_t>(c)], -nrm), 1.0f, 1e-5f);
            EXPECT_NEAR(glm::dot((*g->positions)[static_cast<size_t>((*g->cornerVerts)[static_cast<size_t>(c)])], nrm), 0.0f, 1e-5f);
        }
    }
    EXPECT_EQ(capFaces, 1);
}

TEST(Clip, IdentityWhenNothingIsCutAndEmptyWhenAllIsCut) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "same = clip(b, origin = (-5, 0, 0), normal = (1, 0, 0))\n"
        "gone = clip(b, origin = (5, 0, 0), normal = (1, 0, 0))\n"
        "output same\n"
        "output gone\n");
    expectNoErrors(r);
    pgg::GeoPtr same = geoOutput(r, "same");
    pgg::GeoPtr gone = geoOutput(r, "gone");
    ASSERT_TRUE(same && gone);
    EXPECT_EQ(same->pointCount(), 8u);
    EXPECT_EQ(same->faceCount(), 6u);
    EXPECT_EQ(gone->pointCount(), 0u);
    EXPECT_EQ(gone->faceCount(), 0u);
}

TEST(Clip, PointsKeepHalfSpace) {
    pgg::RunResult r = pgg::run(
        "p = mesh_line(count = 5, length = 4.0, dir = (1, 0, 0))\n"
        "c = clip(p, origin = (1.5, 0, 0), normal = (1, 0, 0))\n"
        "output c\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "c");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->pointCount(), 3u);  // x = 2, 3, 4
    for (const glm::vec3& p : *g->positions) EXPECT_GE(p.x, 1.5f);
}

TEST(Clip, RealizedRowIsCutBrickByBrick) {
    // The masonry use case: a row of boxes over-generated past the wall end,
    // clipped flush at x = 2 -> every brick crossing the plane gets a cap,
    // bricks fully outside vanish, the result is watertight per island.
    pgg::RunResult r = pgg::run(
        "pts = mesh_line(count = 6, length = 5.0, dir = (1, 0, 0))\n"
        "row = realize(instance_on_points(pts, source = box(size = (0.8, 0.4, 0.2))))\n"
        "c = clip(row, origin = (2.0, 0, 0), normal = (-1, 0, 0), cap_group = \"cut\")\n"
        "output c\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "c");
    ASSERT_TRUE(g);
    // Bricks at x = 0, 1 (whole), 2 (cut in half); 3, 4, 5 gone.
    EXPECT_EQ(g->faceCount(), 18u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    EXPECT_NEAR(mx.x, 2.0f, 1e-6f);
    pgg::ConstBoolColumnPtr cut = g->faceGroups->find("cut");
    ASSERT_TRUE(cut);
    int caps = 0;
    for (uint8_t v : *cut) caps += v;
    EXPECT_EQ(caps, 1);
}

TEST(Clip, InstancesAreE204AndZeroNormalIsE612) {
    pgg::RunResult r = pgg::run(
        "pts = mesh_line(count = 2, length = 1.0)\n"
        "inst = instance_on_points(pts, source = box(size = (1, 1, 1)))\n"
        "c = clip(inst, origin = (0, 0, 0), normal = (1, 0, 0))\n"
        "output c\n");
    EXPECT_GE(countCode(r, "E204"), 1);
    pgg::RunResult r2 = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "c = clip(b, origin = (0, 0, 0), normal = (0, 0, 0))\n"
        "output c\n");
    EXPECT_GE(countCode(r2, "E612"), 1);
}

}  // namespace
