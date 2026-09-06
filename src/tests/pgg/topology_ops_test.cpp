// §8.3 topology nodes of v1.21: extrude / inset / separate / triangulate /
// subdivide / merge_by_distance / mirror, plus §8.2 circle + sweep. Counts,
// watertightness, bbox, group/attr propagation and the static errors an agent
// is meant to hit (E204 on wrong kinds, E612 on a zero normal).
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

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

size_t groupCount(const pgg::Geo& g, const char* name) {
    if (!g.faceGroups) return 0;
    pgg::ConstBoolColumnPtr col = g.faceGroups->find(name);
    if (!col) return 0;
    return static_cast<size_t>(std::count(col->begin(), col->end(), uint8_t(1)));
}

// Every face normal points away from `center` (consistent outward winding).
void expectOutward(const pgg::Geo& g, const glm::vec3& center) {
    for (size_t f = 0; f < g.faceCount(); ++f) {
        glm::vec3 fc(0.0f);
        const int32_t b = (*g.faceOffsets)[f], e = (*g.faceOffsets)[f + 1];
        for (int32_t c = b; c < e; ++c)
            fc += (*g.positions)[static_cast<size_t>((*g.cornerVerts)[static_cast<size_t>(c)])];
        fc /= static_cast<float>(e - b);
        EXPECT_GT(glm::dot(pgg::faceNormal(g, f), fc - center), 0.0f) << "face " << f;
    }
}

void bbox(const pgg::Geo& g, glm::vec3& mn, glm::vec3& mx) { pgg::geoBBox(g, mn, mx); }

// --- extrude ------------------------------------------------------------------

TEST(Extrude, RegionOfOneFaceGrowsAClosedBoxAndTagsSidesAndTop) {
    pgg::RunResult r = pgg::run(
        "b = mark(box(size = (1, 1, 1)), \"stone\", where = true, domain = faces)\n"
        "e = extrude(b, distance = 0.5, where = dot(normalize(@N), (0, 1, 0)) > 0.9, side_group = \"side\", top_group = \"top\")\n"
        "output e\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "e");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->pointCount(), 12u);
    EXPECT_EQ(g->faceCount(), 10u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    glm::vec3 mn, mx;
    bbox(*g, mn, mx);
    EXPECT_NEAR(mx.y, 1.0f, 1e-6f);
    EXPECT_NEAR(mn.y, -0.5f, 1e-6f);
    EXPECT_EQ(groupCount(*g, "side"), 4u);
    EXPECT_EQ(groupCount(*g, "top"), 1u);
    // Inherited face group covers the new faces too.
    EXPECT_EQ(groupCount(*g, "stone"), 10u);
    expectOutward(*g, glm::vec3(0.0f, 0.25f, 0.0f));
}

TEST(Extrude, RegionOfTwoAdjacentFacesSharesTheInnerEdge) {
    // +y and +x faces as one region: the shared edge is interior, so 6 side
    // quads (not 8) and the region moves along the averaged normal.
    pgg::RunResult r = pgg::run(
        "b = compute_normals(box(size = (1, 1, 1)), mode = flat)\n"
        "e = extrude(b, distance = 0.3, where = dot(@N, (1, 1, 0)) > 0.5)\n"
        "output e\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "e");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->faceCount(), 12u);  // 4 untouched + 2 moved + 6 sides
    EXPECT_EQ(g->pointCount(), 14u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
}

TEST(Extrude, IndividualExtrudesEveryFaceOnItsOwnNormalAndStaysClosed) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "e = extrude(b, distance = 0.25, mode = individual)\n"
        "output e\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "e");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->pointCount(), 32u);
    EXPECT_EQ(g->faceCount(), 30u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    glm::vec3 mn, mx;
    bbox(*g, mn, mx);
    EXPECT_NEAR(mx.x, 0.75f, 1e-6f);
    EXPECT_NEAR(mn.z, -0.75f, 1e-6f);
    expectOutward(*g, glm::vec3(0.0f));
}

TEST(Extrude, NegativeDistancePushesInwardAndEmptySelectionIsIdentity) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "inw = extrude(b, distance = -0.2, where = dot(normalize(@N), (0, 1, 0)) > 0.9)\n"
        "id = extrude(b, distance = 0.5, where = false)\n"
        "output inw\n"
        "output id\n");
    expectNoErrors(r);
    pgg::GeoPtr inw = geoOutput(r, "inw");
    pgg::GeoPtr id = geoOutput(r, "id");
    ASSERT_TRUE(inw && id);
    glm::vec3 mn, mx;
    bbox(*inw, mn, mx);
    EXPECT_NEAR(mx.y, 0.5f, 1e-6f);  // rim stays, the cap sinks
    EXPECT_EQ(inw->faceCount(), 10u);
    EXPECT_EQ(id->faceCount(), 6u);
    EXPECT_EQ(id->pointCount(), 8u);
}

TEST(Extrude, PointAttrsCopyToTheLiftedRowsAndOnlyMeshesAreAccepted) {
    pgg::RunResult r = pgg::run(
        "b = set(box(size = (1, 1, 1)), \"h\", dot(@P, (0, 1, 0)), domain = points)\n"
        "e = extrude(b, distance = 1.0, where = dot(normalize(@N), (0, 1, 0)) > 0.9)\n"
        "top = count(e, where = @h > 0.4)\n"
        "output e\n"
        "output top\n");
    expectNoErrors(r);
    // The 4 old top points keep h = 0.5 and the 4 lifted copies inherit it.
    for (const auto& o : r.outputs)
        if (o.name == "top") EXPECT_EQ(pgg::asInt(o.value), 8);

    pgg::RunResult bad = pgg::run(
        "p = mesh_line(count = 4, length = 1.0)\n"
        "e = extrude(p, distance = 1.0)\n"
        "output e\n");
    EXPECT_EQ(countCode(bad, "E204"), 1);
}

// --- inset --------------------------------------------------------------------

TEST(Inset, EveryFaceGetsAnInnerPanelAndARim) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "i = inset(b, amount = 0.1, depth = -0.05, inner_group = \"inner\", rim_group = \"rim\")\n"
        "output i\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "i");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->pointCount(), 32u);
    EXPECT_EQ(g->faceCount(), 30u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    EXPECT_EQ(groupCount(*g, "inner"), 6u);
    EXPECT_EQ(groupCount(*g, "rim"), 24u);
    // Negative depth: the panels sink, so the bbox is still the unit box.
    glm::vec3 mn, mx;
    bbox(*g, mn, mx);
    EXPECT_NEAR(mx.x, 0.5f, 1e-6f);
    EXPECT_NEAR(mn.y, -0.5f, 1e-6f);
    // Inner points of the +y face sit at y = 0.45 and |x|,|z| = 0.4.
    int inner = 0;
    for (const glm::vec3& p : *g->positions)
        if (std::fabs(p.y - 0.45f) < 1e-5f) {
            inner += 1;
            EXPECT_NEAR(std::fabs(p.x), 0.4f, 1e-5f);
            EXPECT_NEAR(std::fabs(p.z), 0.4f, 1e-5f);
        }
    EXPECT_EQ(inner, 4);
}

TEST(Inset, SelectionAndPositiveDepthAndCornerAttrInterpolation) {
    pgg::RunResult r = pgg::run(
        "b0 = compute_normals(box(size = (2, 2, 2)), mode = flat)\n"
        "b = set(b0, \"u\", dot(@P, (1, 0, 0)), domain = corners)\n"
        "i = inset(b, amount = 0.5, depth = 0.25, where = dot(@N, (0, 1, 0)) > 0.9)\n"
        "output i\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "i");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->faceCount(), 10u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    glm::vec3 mn, mx;
    bbox(*g, mn, mx);
    EXPECT_NEAR(mx.y, 1.25f, 1e-6f);
    // Corner attrs of the inner polygon and the rim copy their source corner
    // (no interpolation): every u is still ±1.
    const pgg::AttrSet* ca = g->attrs(pgg::Domain::Corners);
    ASSERT_TRUE(ca);
    const pgg::AttrColumn* u = ca->find("u");
    ASSERT_TRUE(u);
    const auto& uv = *std::get<std::shared_ptr<const std::vector<float>>>(u->data);
    EXPECT_EQ(uv.size(), g->cornerCount());
    for (float v : uv) EXPECT_NEAR(std::fabs(v), 1.0f, 1e-5f);
}

// --- separate / triangulate ---------------------------------------------------

TEST(Separate, SplitsByMaskWithDeleteCascade) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "yes, no = separate(b, where = dot(normalize(@N), (0, 1, 0)) > 0.9, domain = faces)\n"
        "py, pn = separate(b, where = dot(@P, (0, 1, 0)) > 0.0, domain = points)\n"
        "output yes\n"
        "output no\n"
        "output py\n"
        "output pn\n");
    expectNoErrors(r);
    pgg::GeoPtr yes = geoOutput(r, "yes"), no = geoOutput(r, "no");
    pgg::GeoPtr py = geoOutput(r, "py"), pn = geoOutput(r, "pn");
    ASSERT_TRUE(yes && no && py && pn);
    EXPECT_EQ(yes->faceCount(), 1u);
    EXPECT_EQ(no->faceCount(), 5u);
    // Face masks keep points; point masks cascade to faces.
    EXPECT_EQ(yes->pointCount(), 8u);
    EXPECT_EQ(py->pointCount(), 4u);
    EXPECT_EQ(py->faceCount(), 1u);
    EXPECT_EQ(pn->pointCount(), 4u);
    EXPECT_EQ(pn->faceCount(), 1u);
}

TEST(Triangulate, NgonsBecomeFansKeepingGroupsAndWatertightness) {
    pgg::RunResult r = pgg::run(
        "b = mark(box(size = (1, 1, 1)), \"top\", where = dot(normalize(@N), (0, 1, 0)) > 0.9, domain = faces)\n"
        "t = triangulate(b)\n"
        "output t\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "t");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->pointCount(), 8u);
    EXPECT_EQ(g->faceCount(), 12u);
    EXPECT_EQ(g->cornerCount(), 36u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    EXPECT_EQ(groupCount(*g, "top"), 2u);
    expectOutward(*g, glm::vec3(0.0f));
}

// --- subdivide ----------------------------------------------------------------

TEST(Subdivide, LinearKeepsTheShapeAndQuadruplesFaces) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "s = subdivide(b, level = 1, scheme = linear)\n"
        "s2 = subdivide(b, level = 2, scheme = linear)\n"
        "output s\n"
        "output s2\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "s"), g2 = geoOutput(r, "s2");
    ASSERT_TRUE(g && g2);
    EXPECT_EQ(g->pointCount(), 26u);
    EXPECT_EQ(g->faceCount(), 24u);
    EXPECT_EQ(g->cornerCount(), 96u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    EXPECT_EQ(g2->faceCount(), 96u);
    EXPECT_EQ(g2->pointCount(), 98u);
    glm::vec3 mn, mx;
    bbox(*g2, mn, mx);
    EXPECT_NEAR(mn.x, -0.5f, 1e-6f);
    EXPECT_NEAR(mx.z, 0.5f, 1e-6f);
    expectOutward(*g2, glm::vec3(0.0f));
}

TEST(Subdivide, CatmullClarkRoundsTheCubeCorners) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "s = subdivide(b, level = 1)\n"
        "output s\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "s");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->pointCount(), 26u);
    EXPECT_EQ(g->faceCount(), 24u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    // Cube corners (valence 3) move to 5/18 = 0.2778 of the half size; face
    // points stay on the axes at 0.5.
    glm::vec3 mn, mx;
    bbox(*g, mn, mx);
    EXPECT_NEAR(mx.x, 0.5f, 1e-5f);
    int corners = 0;
    for (const glm::vec3& p : *g->positions)
        if (std::fabs(std::fabs(p.x) - 0.27778f) < 1e-3f && std::fabs(std::fabs(p.y) - 0.27778f) < 1e-3f &&
            std::fabs(std::fabs(p.z) - 0.27778f) < 1e-3f)
            corners += 1;
    EXPECT_EQ(corners, 8);
    expectOutward(*g, glm::vec3(0.0f));
}

TEST(Subdivide, LoopNeedsTrianglesAndStaysClosed) {
    pgg::RunResult r = pgg::run(
        "b = triangulate(box(size = (1, 1, 1)))\n"
        "s = subdivide(b, level = 1, scheme = loop)\n"
        "output s\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "s");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->faceCount(), 48u);
    EXPECT_EQ(g->pointCount(), 26u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*g), 0u);
    expectOutward(*g, glm::vec3(0.0f));

    pgg::RunResult quads = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "s = subdivide(b, scheme = loop)\n"
        "output s\n");
    EXPECT_TRUE(quads.hasErrors());
}

TEST(Subdivide, SmoothSchemesMarkNormalsStaleAndLevelZeroIsIdentity) {
    pgg::RunResult r = pgg::run(
        "b = compute_normals(box(size = (1, 1, 1)), mode = flat)\n"
        "s = subdivide(b, level = 1)\n"
        "n = count(s, where = dot(@N, (0, 1, 0)) > 2)\n"
        "z = subdivide(b, level = 0)\n"
        "output s\n"
        "output n\n"
        "output z\n");
    EXPECT_FALSE(r.hasErrors());
    EXPECT_EQ(countCode(r, "W006"), 1);
    pgg::GeoPtr z = geoOutput(r, "z");
    ASSERT_TRUE(z);
    EXPECT_EQ(z->faceCount(), 6u);
    EXPECT_EQ(z->pointCount(), 8u);
}

// --- merge_by_distance --------------------------------------------------------

TEST(MergeByDistance, WeldsCoincidentPointsOfTwoBoxesAndPointClouds) {
    pgg::RunResult r = pgg::run(
        "a = box(size = (1, 1, 1))\n"
        "b = transform(a, translate = (1, 0, 0))\n"
        "w = merge_by_distance(merge(a, b), dist = 0.001)\n"
        "far = merge_by_distance(merge(a, transform(a, translate = (1.01, 0, 0))), dist = 0.001)\n"
        "p0 = mesh_line(count = 5, length = 1.0)\n"
        "p = merge_by_distance(merge(p0, p0), dist = 0.001)\n"
        "output w\n"
        "output far\n"
        "output p\n");
    expectNoErrors(r);
    pgg::GeoPtr w = geoOutput(r, "w"), far = geoOutput(r, "far"), p = geoOutput(r, "p");
    ASSERT_TRUE(w && far && p);
    EXPECT_EQ(w->pointCount(), 12u);
    EXPECT_EQ(w->faceCount(), 12u);  // faces are kept, only points are welded
    EXPECT_EQ(w->cornerCount(), 48u);
    EXPECT_EQ(far->pointCount(), 16u);
    EXPECT_EQ(p->kind, pgg::GeoKind::Points);
    EXPECT_EQ(p->pointCount(), 5u);
}

TEST(MergeByDistance, TheLowestIndexPointOfAClusterSurvivesWithItsAttrs) {
    // Deterministic "first wins": position and attrs of the lowest index, no
    // averaging (a's w = 1 survives, b's w = 3 is dropped).
    pgg::RunResult r = pgg::run(
        "a = set(mesh_line(count = 2, length = 1.0), \"w\", 1.0, domain = points)\n"
        "b = set(mesh_line(count = 2, length = 1.0), \"w\", 3.0, domain = points)\n"
        "m = merge_by_distance(merge(a, b), dist = 0.01)\n"
        "s = sum_of(@w, on = m)\n"
        "output m\n"
        "output s\n");
    expectNoErrors(r);
    pgg::GeoPtr m = geoOutput(r, "m");
    ASSERT_TRUE(m);
    EXPECT_EQ(m->pointCount(), 2u);
    for (const auto& o : r.outputs)
        if (o.name == "s") EXPECT_NEAR(pgg::asF32(o.value), 2.0f, 1e-5f);
}

// --- mirror -------------------------------------------------------------------

TEST(Mirror, HalfBoxBecomesAClosedBoxWithASharedSeamAndOutwardWinding) {
    // Half box = 5 faces (the 4 cut faces are half-quads) -> 10 faces after
    // the mirror. Points on the plane (|d| <= weld, weld = 0 catches exact
    // ones) are shared, so the seam is welded by default; an off-plane seam
    // needs an explicit weld.
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "half = clip(b, origin = (0, 0, 0), normal = (1, 0, 0), cap_group = \"cut\")\n"
        "open = delete(half, where = ingroup(\"cut\"), domain = faces)\n"
        "m = mirror(open)\n"
        "shifted = transform(open, translate = (0.0005, 0, 0))\n"
        "gap = mirror(shifted)\n"
        "welded = mirror(shifted, weld = 0.001)\n"
        "output m\n"
        "output gap\n"
        "output welded\n");
    expectNoErrors(r);
    pgg::GeoPtr m = geoOutput(r, "m"), gap = geoOutput(r, "gap"), welded = geoOutput(r, "welded");
    ASSERT_TRUE(m && gap && welded);
    EXPECT_EQ(m->pointCount(), 12u);
    EXPECT_EQ(m->faceCount(), 10u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*m), 0u);
    glm::vec3 mn, mx;
    bbox(*m, mn, mx);
    EXPECT_NEAR(mn.x, -0.5f, 1e-6f);
    EXPECT_NEAR(mx.x, 0.5f, 1e-6f);
    expectOutward(*m, glm::vec3(0.0f));
    EXPECT_EQ(gap->pointCount(), 16u);
    EXPECT_NE(pgg::nonManifoldEdgeCount(*gap), 0u);
    EXPECT_EQ(welded->pointCount(), 12u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*welded), 0u);
}

TEST(Mirror, ReflectsNormalTaggedAttrsAndAcceptsPointsAndAnyPlane) {
    pgg::RunResult r = pgg::run(
        "b = compute_normals(transform(box(size = (1, 1, 1)), translate = (0, 2, 0)), mode = flat)\n"
        "m = mirror(b, origin = (0, 0.5, 0), normal = (0, 1, 0))\n"
        "up = count(m, where = dot(@N, (0, 1, 0)) > 0.9, domain = faces)\n"
        "down = count(m, where = dot(@N, (0, -1, 0)) > 0.9, domain = faces)\n"
        "p = mirror(mesh_line(count = 3, length = 1.0, dir = (1, 0, 0)), origin = (1, 0, 0), normal = (1, 0, 0))\n"
        "output m\n"
        "output up\n"
        "output down\n"
        "output p\n");
    expectNoErrors(r);
    pgg::GeoPtr m = geoOutput(r, "m"), p = geoOutput(r, "p");
    ASSERT_TRUE(m && p);
    EXPECT_EQ(m->faceCount(), 12u);
    glm::vec3 mn, mx;
    bbox(*m, mn, mx);
    EXPECT_NEAR(mx.y, 2.5f, 1e-5f);
    EXPECT_NEAR(mn.y, -1.5f, 1e-5f);
    // Corner @N (flat) of the mirrored copy is reflected: one +y face and one
    // -y face per box side.
    int64_t up = -1, down = -1;
    for (const auto& o : r.outputs) {
        if (o.name == "up") up = pgg::asInt(o.value);
        if (o.name == "down") down = pgg::asInt(o.value);
    }
    EXPECT_EQ(up, 2);
    EXPECT_EQ(down, 2);
    // The point at x = 1 lies on the plane and is shared: 3 + 2.
    EXPECT_EQ(p->kind, pgg::GeoKind::Points);
    EXPECT_EQ(p->pointCount(), 5u);
    bbox(*p, mn, mx);
    EXPECT_NEAR(mx.x, 2.0f, 1e-5f);
}

TEST(Mirror, ZeroNormalIsE612AndInstancesAreE204) {
    pgg::RunResult zero = pgg::run(
        "m = mirror(box(size = (1, 1, 1)), normal = (0, 0, 0))\n"
        "output m\n");
    EXPECT_EQ(countCode(zero, "E612"), 1);
    pgg::RunResult inst = pgg::run(
        "i = instance_on_points(mesh_line(count = 2, length = 1.0), box(size = (1, 1, 1)))\n"
        "m = mirror(i)\n"
        "output m\n");
    EXPECT_EQ(countCode(inst, "E204"), 1);
}

// --- circle / sweep -----------------------------------------------------------

TEST(Circle, PointsOnTheXYRingAndSidesBelowThreeAreE204) {
    // The profile plane is XY (z = 0), the plane sweep() reads its profile in.
    pgg::RunResult r = pgg::run(
        "c = circle(sides = 8, radius = 2.0)\n"
        "output c\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "c");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->kind, pgg::GeoKind::Points);
    EXPECT_EQ(g->pointCount(), 8u);
    for (const glm::vec3& p : *g->positions) {
        EXPECT_NEAR(p.z, 0.0f, 1e-6f);
        EXPECT_NEAR(glm::length(glm::vec2(p.x, p.y)), 2.0f, 1e-5f);
    }
    pgg::RunResult bad = pgg::run("c = circle(sides = 2)\noutput c\n");
    EXPECT_EQ(countCode(bad, "E204"), 1);
}

TEST(Sweep, StraightTubeIsClosedWithCapsAndCarriesPathAttrs) {
    pgg::RunResult r = pgg::run(
        "path0 = mesh_line(count = 5, length = 4.0, dir = (0, 1, 0))\n"
        "path = set(path0, \"h\", dot(@P, (0, 1, 0)), domain = points)\n"
        "tube = sweep(path, circle(sides = 8, radius = 0.2))\n"
        "nocap = sweep(path, circle(sides = 8, radius = 0.2), cap = false)\n"
        "top = count(tube, where = @h > 3.9)\n"
        "output tube\n"
        "output nocap\n"
        "output top\n");
    expectNoErrors(r);
    pgg::GeoPtr tube = geoOutput(r, "tube"), nocap = geoOutput(r, "nocap");
    ASSERT_TRUE(tube && nocap);
    EXPECT_EQ(tube->pointCount(), 40u);
    EXPECT_EQ(tube->faceCount(), 34u);  // 4 * 8 quads + 2 caps
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*tube), 0u);
    glm::vec3 mn, mx;
    bbox(*tube, mn, mx);
    EXPECT_NEAR(mn.y, 0.0f, 1e-5f);
    EXPECT_NEAR(mx.y, 4.0f, 1e-5f);
    EXPECT_NEAR(mx.x, 0.2f, 1e-5f);
    EXPECT_NEAR(mn.z, -0.2f, 1e-5f);
    expectOutward(*tube, glm::vec3(0.0f, 2.0f, 0.0f));
    EXPECT_EQ(nocap->faceCount(), 32u);
    // The whole last ring inherits the path point's h.
    for (const auto& o : r.outputs)
        if (o.name == "top") EXPECT_EQ(pgg::asInt(o.value), 8);
}

TEST(Sweep, ClosedPathMakesATorusAndScaleAttrScalesTheProfile) {
    pgg::RunResult r = pgg::run(
        "ring = transform(circle(sides = 12, radius = 2.0), rotate = (90, 0, 0))\n"
        "torus = sweep(ring, circle(sides = 6, radius = 0.25), closed = true)\n"
        "path = set(mesh_line(count = 3, length = 2.0, dir = (0, 1, 0)), \"scale\", 1.0 + dot(@P, (0, 1, 0)), domain = points)\n"
        "cone = sweep(path, circle(sides = 4, radius = 0.1), cap = false)\n"
        "output torus\n"
        "output cone\n");
    expectNoErrors(r);
    pgg::GeoPtr torus = geoOutput(r, "torus"), cone = geoOutput(r, "cone");
    ASSERT_TRUE(torus && cone);
    EXPECT_EQ(torus->pointCount(), 72u);
    EXPECT_EQ(torus->faceCount(), 72u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*torus), 0u);
    glm::vec3 mn, mx;
    bbox(*torus, mn, mx);
    // Hexagon profile: the outer extent is 2 + 0.25 * cos(phase) with the
    // transported phase, i.e. within [2 + 0.25 * cos(30deg), 2.25].
    EXPECT_GE(mx.x, 2.216f);
    EXPECT_LE(mx.x, 2.2501f);
    EXPECT_NEAR(mx.y, 0.25f, 1e-2f);
    // @scale 1 -> 3 along the path: top ring radius 0.3, bottom 0.1.
    bbox(*cone, mn, mx);
    EXPECT_NEAR(mx.x, 0.3f, 1e-4f);
    int bottom = 0;
    for (const glm::vec3& p : *cone->positions)
        if (std::fabs(p.y) < 1e-5f) {
            bottom += 1;
            EXPECT_NEAR(glm::length(glm::vec2(p.x, p.z)), 0.1f, 1e-4f);
        }
    EXPECT_EQ(bottom, 4);
}

TEST(Sweep, TooShortPathOrProfileIsE204) {
    pgg::RunResult p1 = pgg::run(
        "t = sweep(mesh_line(count = 1, length = 1.0), circle(sides = 6))\n"
        "output t\n");
    EXPECT_EQ(countCode(p1, "E204"), 1);
    pgg::RunResult pr = pgg::run(
        "t = sweep(mesh_line(count = 4, length = 1.0), mesh_line(count = 2, length = 1.0))\n"
        "output t\n");
    EXPECT_EQ(countCode(pr, "E204"), 1);
}

}  // namespace
