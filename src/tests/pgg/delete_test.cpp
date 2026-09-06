// §8.3 delete: element removal by mask with the cascade contract — a deleted
// point takes its faces, a deleted face takes its corners (points stay), a
// deleted corner takes its face; columns on every surviving domain are
// gathered so attributes/groups/@N stay aligned; points/instances accept only
// domain = points; detail is rejected (E204).
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

TEST(Delete, PointsMaskOnPointsKeepsAlignedColumns) {
    // A 5-point line, drop x > 2.5 (the last two); @scale and a points group
    // must be gathered along with @P.
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 5, length = 4.0, dir = (1, 0, 0))\n"
        "s = set(l, \"scale\", dot(@P, (1, 0, 0)) * 10.0)\n"
        "g = mark(s, \"odd\", where = (@index - 2 * floor(@index / 2)) > 0)\n"
        "d = delete(g, where = dot(@P, (1, 0, 0)) > 2.5)\n"
        "output d\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "d");
    ASSERT_TRUE(g);
    ASSERT_EQ(g->pointCount(), 3u);
    for (size_t i = 0; i < 3; ++i) EXPECT_NEAR((*g->positions)[i].x, static_cast<float>(i), 1e-6f);
    const pgg::AttrColumn* sc = g->pointAttrs ? g->pointAttrs->find("scale") : nullptr;
    ASSERT_TRUE(sc);
    const auto& scale = *std::get<std::shared_ptr<const std::vector<float>>>(sc->data);
    ASSERT_EQ(scale.size(), 3u);
    EXPECT_NEAR(scale[2], 20.0f, 1e-4f);
    pgg::ConstBoolColumnPtr odd = g->pointGroups ? g->pointGroups->find("odd") : nullptr;
    ASSERT_TRUE(odd);
    ASSERT_EQ(odd->size(), 3u);
    EXPECT_EQ((*odd)[0], 0);
    EXPECT_EQ((*odd)[1], 1);
    EXPECT_EQ((*odd)[2], 0);
}

TEST(Delete, PointsMaskOnMeshCascadesToFaces) {
    // Deleting the 4 top points of a box kills every face touching them: only
    // the bottom face survives, its corners remapped to the 4 kept points.
    pgg::RunResult r = pgg::run(
        "b = box(size = (2, 2, 2))\n"
        "m = mark(b, \"bottom\", where = dot(@N, (0, -1, 0)) > 0.5, domain = faces)\n"
        "d = delete(m, where = dot(@P, (0, 1, 0)) > 0)\n"
        "output d\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "d");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->pointCount(), 4u);
    ASSERT_EQ(g->faceCount(), 1u);
    EXPECT_EQ(g->cornerCount(), 4u);
    for (int32_t v : *g->cornerVerts) {
        ASSERT_GE(v, 0);
        ASSERT_LT(v, 4);
        EXPECT_LT((*g->positions)[static_cast<size_t>(v)].y, 0.0f);
    }
    ASSERT_TRUE(g->normals);
    EXPECT_EQ(g->normals->size(), 4u);
    pgg::ConstBoolColumnPtr bottom = g->faceGroups ? g->faceGroups->find("bottom") : nullptr;
    ASSERT_TRUE(bottom);
    ASSERT_EQ(bottom->size(), 1u);
    EXPECT_EQ((*bottom)[0], 1);
}

TEST(Delete, FacesMaskKeepsPointsAndGathersFaceGroups) {
    // Deleting the top face leaves 5 faces and all 8 points (orphans allowed).
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "m = mark(b, \"side\", where = abs(dot(@N, (0, 1, 0))) < 0.5, domain = faces)\n"
        "d = delete(m, where = dot(@N, (0, 1, 0)) > 0.5, domain = faces)\n"
        "output d\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "d");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->pointCount(), 8u);
    ASSERT_EQ(g->faceCount(), 5u);
    EXPECT_EQ(g->cornerCount(), 20u);
    pgg::ConstBoolColumnPtr side = g->faceGroups ? g->faceGroups->find("side") : nullptr;
    ASSERT_TRUE(side);
    int sides = 0;
    for (uint8_t v : *side) sides += v;
    EXPECT_EQ(sides, 4);
}

TEST(Delete, CornersMaskTakesTheWholeFace) {
    // Corner 0 belongs to exactly one face: that face goes, nothing else.
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "d = delete(b, where = @index == 0, domain = corners)\n"
        "output d\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "d");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->faceCount(), 5u);
    EXPECT_EQ(g->cornerCount(), 20u);
    EXPECT_EQ(g->pointCount(), 8u);
}

TEST(Delete, AllFalseMaskIsIdentity) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (1, 1, 1))\n"
        "same = delete(b, where = false, domain = faces)\n"
        "output same\n");
    expectNoErrors(r);
    pgg::GeoPtr same = geoOutput(r, "same");
    ASSERT_TRUE(same);
    EXPECT_EQ(same->faceCount(), 6u);
}

TEST(Delete, FacesDomainOnPointsIsE204) {
    // Static mirror (schema kind is known) and the runtime check agree.
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 3, length = 1.0)\n"
        "bad = delete(l, where = true, domain = faces)\n"
        "output bad\n");
    EXPECT_TRUE(r.hasErrors());
    EXPECT_GE(countCode(r, "E204"), 1);
}

TEST(Delete, InstancesDropAnchorsBeforeRealize) {
    // Anchors on x > 0 are removed; realize stamps only the two survivors.
    pgg::RunResult r = pgg::run(
        "pts = transform(mesh_line(count = 4, length = 3.0, dir = (1, 0, 0)), translate = (-1.5, 0, 0))\n"
        "inst = instance_on_points(pts, source = box(size = (0.2, 0.2, 0.2)))\n"
        "cut = delete(inst, where = dot(@P, (1, 0, 0)) > 0)\n"
        "m = realize(cut)\n"
        "output m\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "m");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->faceCount(), 12u);
    for (const glm::vec3& p : *g->positions) EXPECT_LT(p.x, 0.0f);
}

}  // namespace
