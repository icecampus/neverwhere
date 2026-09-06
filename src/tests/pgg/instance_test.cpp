// §8.8 instancing tests (E2 acceptance): copy-stamp (@scale/@orient/@variant/
// @tint with defaults), the variants list selecting the source, realize
// (counts, transform application, tint materialization), geometry sharing by
// pointer, 10^5 instances without realize, and merge/E204.
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>

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

TEST(Instance, CopyStampCarriesAttributesAndSharesAnchors) {
    pgg::RunResult r = pgg::run(
        "root_rng = rng_from_seed(3)\n"
        "stamp_rng = split_rng(root_rng, key = \"stamp\")\n"
        "l = mesh_line(count = 4, length = 3.0)\n"
        "p1 = set(l, \"scale\", random(lo = 0.5, hi = 1.5, rng = stamp_rng))\n"
        "p2 = set(p1, \"orient\", orient_from_euler(random_vec(lo = (0, 0, 0), hi = (0, 360, 0), rng = stamp_rng)))\n"
        "p3 = set(p2, \"tint\", (0.8, 0.3, 0.3))\n"
        "p = set(p3, \"variant\", @index % 2)\n"
        "src = ico_sphere(subdiv = 1, radius = 0.5)\n"
        "inst = instance_on_points(p, src)\n"
        "output inst\n"
        "output p\n"
        "output src\n");
    expectNoErrors(r);
    pgg::GeoPtr inst = geoOutput(r, "inst");
    pgg::GeoPtr pts = geoOutput(r, "p");
    pgg::GeoPtr src = geoOutput(r, "src");
    ASSERT_TRUE(inst);
    ASSERT_EQ(inst->kind, pgg::GeoKind::Instances);
    // Anchors are the point positions, shared by pointer (no copies).
    EXPECT_EQ(inst->positions.get(), pts->positions.get());
    // Stamp attributes live on the instances (constant tint inferred detail,
    // the rest on points).
    ASSERT_TRUE(inst->pointAttrs);
    EXPECT_TRUE(inst->pointAttrs->find("scale"));
    EXPECT_TRUE(inst->pointAttrs->find("orient"));
    EXPECT_TRUE(inst->pointAttrs->find("variant"));
    EXPECT_TRUE(inst->detailAttrs && inst->detailAttrs->find("tint"));
    // Without a variants list the single source is variant 0, shared.
    ASSERT_TRUE(inst->instanceSources);
    ASSERT_EQ(inst->instanceSources->size(), 1u);
    EXPECT_EQ((*inst->instanceSources)[0].get(), src.get());
}

TEST(Instance, VariantsListSelectsSource) {
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 4, length = 3.0)\n"
        "p = set(l, \"variant\", @index % 2)\n"
        "va = ico_sphere(subdiv = 1, radius = 0.2)\n"
        "vb = ico_sphere(subdiv = 2, radius = 0.2)\n"
        "inst = instance_on_points(p, va, variants = [va, vb])\n"
        "real = realize(inst)\n"
        "output inst\n"
        "output real\n"
        "output va\n"
        "output vb\n");
    expectNoErrors(r);
    pgg::GeoPtr inst = geoOutput(r, "inst");
    pgg::GeoPtr real = geoOutput(r, "real");
    pgg::GeoPtr va = geoOutput(r, "va");
    pgg::GeoPtr vb = geoOutput(r, "vb");
    ASSERT_TRUE(inst->instanceSources);
    ASSERT_EQ(inst->instanceSources->size(), 2u);
    EXPECT_EQ((*inst->instanceSources)[0].get(), va.get());
    EXPECT_EQ((*inst->instanceSources)[1].get(), vb.get());
    // variant = @index % 2 -> two instances of va (42 pts) + two of vb (162).
    EXPECT_EQ(real->pointCount(), 2 * va->pointCount() + 2 * vb->pointCount());
    EXPECT_EQ(real->faceCount(), 2 * va->faceCount() + 2 * vb->faceCount());
}

TEST(Instance, RealizeDefaultsAreIdentity) {
    // No stamps at all: scale 1, identity orient, variant 0, white tint.
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 2, length = 10.0, dir = (1, 0, 0))\n"
        "src = box(size = (2, 2, 2))\n"
        "inst = instance_on_points(l, src)\n"
        "real = realize(inst)\n"
        "output real\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "real");
    ASSERT_TRUE(g);
    EXPECT_EQ(g->pointCount(), 16u);
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    EXPECT_NEAR(mn.x, -1.0f, 1e-6f);
    EXPECT_NEAR(mn.y, -1.0f, 1e-6f);
    EXPECT_NEAR(mn.z, -1.0f, 1e-6f);
    EXPECT_NEAR(mx.x, 11.0f, 1e-5f);
    EXPECT_NEAR(mx.y, 1.0f, 1e-6f);
    EXPECT_NEAR(mx.z, 1.0f, 1e-6f);
    // tint materialized, default white.
    const pgg::AttrColumn* tint = g->pointAttrs ? g->pointAttrs->find("tint") : nullptr;
    ASSERT_TRUE(tint);
    const auto& tv = std::get<std::shared_ptr<const std::vector<glm::vec3>>>(tint->data);
    ASSERT_EQ(tv->size(), 16u);
    for (const glm::vec3& t : *tv) EXPECT_EQ(t, glm::vec3(1.0f));
}

TEST(Instance, RealizeAppliesScaleOrientTintInIndexOrder) {
    // One anchor at the origin: scale 2 + 90-degree Y rotation + a tint.
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 1, length = 0.0)\n"
        "p1 = set(l, \"scale\", 2.0)\n"
        "p2 = set(p1, \"orient\", orient_from_euler((0, 90, 0)))\n"
        "p = set(p2, \"tint\", (0.25, 0.5, 0.75))\n"
        "src = box(size = (2, 4, 6))\n"
        "real = realize(instance_on_points(p, src))\n"
        "output real\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "real");
    ASSERT_TRUE(g);
    // Box corner (1,2,3): scale 2 -> (2,4,6); R_y(90): (x,z)->(z,-x) -> (6,4,-2).
    bool found = false;
    for (const glm::vec3& p : *g->positions)
        if (glm::length(p - glm::vec3(6, 4, -2)) < 1e-4f) found = true;
    EXPECT_TRUE(found);
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    EXPECT_NEAR(mn.x, -6.0f, 1e-4f);
    EXPECT_NEAR(mx.x, 6.0f, 1e-4f);
    EXPECT_NEAR(mn.z, -2.0f, 1e-4f);
    EXPECT_NEAR(mx.z, 2.0f, 1e-4f);
    const pgg::AttrColumn* tint = g->pointAttrs ? g->pointAttrs->find("tint") : nullptr;
    ASSERT_TRUE(tint);
    const auto& tv = std::get<std::shared_ptr<const std::vector<glm::vec3>>>(tint->data);
    for (const glm::vec3& t : *tv) EXPECT_EQ(t, glm::vec3(0.25f, 0.5f, 0.75f));

    // @index order: with two anchors the first instance's vertices come first.
    pgg::RunResult two = pgg::run(
        "l = mesh_line(count = 2, length = 10.0, dir = (1, 0, 0))\n"
        "src = box(size = (2, 2, 2))\n"
        "real = realize(instance_on_points(l, src))\n"
        "output real\n");
    expectNoErrors(two);
    pgg::GeoPtr g2 = geoOutput(two, "real");
    // Vertex 0 belongs to the instance at anchor (0,0,0) (x near -1..1);
    // the last vertex belongs to the instance at (10,0,0) (x near 9..11).
    EXPECT_NEAR((*g2->positions)[0].x, 0.0f, 1.5f);
    EXPECT_NEAR(g2->positions->back().x, 10.0f, 1.5f);
}

TEST(Instance, TenToTheFifthInstancesWithoutRealize) {
    // E2 acceptance: 10^5 lightweight instances — sources shared by pointer,
    // no realize, the output computes quickly.
    const auto t0 = std::chrono::steady_clock::now();
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 100000, length = 1000.0)\n"
        "p = set(l, \"scale\", 2.0)\n"
        "src = ico_sphere(subdiv = 2, radius = 1.0)\n"
        "inst = instance_on_points(p, src)\n"
        "output inst\n");
    const double sec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    expectNoErrors(r);
    pgg::GeoPtr inst = geoOutput(r, "inst");
    ASSERT_TRUE(inst);
    EXPECT_EQ(inst->pointCount(), 100000u);
    ASSERT_TRUE(inst->instanceSources);
    EXPECT_EQ(inst->instanceSources->size(), 1u);
    // Cheap: anything near a copy of the sources would take orders longer.
    EXPECT_LT(sec, 5.0);
}

TEST(Merge, MeshAndPointsConcat) {
    pgg::RunResult r = pgg::run(
        "a = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "b = box(size = (2, 2, 2))\n"
        "m = merge(a, b)\n"
        "l1 = mesh_line(count = 3, length = 2.0)\n"
        "l2 = mesh_line(count = 5, length = 4.0)\n"
        "mp = merge(l1, l2)\n"
        "output m\n"
        "output mp\n");
    expectNoErrors(r);
    pgg::GeoPtr m = geoOutput(r, "m");
    ASSERT_TRUE(m);
    EXPECT_EQ(m->kind, pgg::GeoKind::Mesh);
    EXPECT_EQ(m->pointCount(), 42u + 8u);
    EXPECT_EQ(m->faceCount(), 80u + 6u);
    EXPECT_EQ(m->cornerCount(), 240u + 24u);
    pgg::GeoPtr mp = geoOutput(r, "mp");
    ASSERT_TRUE(mp);
    EXPECT_EQ(mp->kind, pgg::GeoKind::Points);
    EXPECT_EQ(mp->pointCount(), 8u);
}

TEST(Merge, ColumnUnionZeroFills) {
    // A points attribute existing only on one side zero-fills on the other.
    pgg::RunResult r = pgg::run(
        "a = mesh_line(count = 2, length = 1.0, dir = (1, 0, 0))\n"
        "sa = set(a, \"x\", dot(@P, (1, 0, 0)))\n"
        "b = mesh_line(count = 3, length = 2.0)\n"
        "m = merge(sa, b)\n"
        "s = sum_of(@x, on = m)\n"
        "output s\n");
    expectNoErrors(r);
    EXPECT_NEAR(pgg::asF32(r.outputs[0].value), 1.0f, 1e-6f);  // 0 + 1 + 3 * 0
}

TEST(Merge, KindMismatchIsE204) {
    pgg::RunResult r = pgg::run(
        "a = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "b = mesh_line(count = 3, length = 2.0)\n"
        "m = merge(a, b)\n"
        "output m\n");
    EXPECT_EQ(countCode(r, "E204"), 1);
}

TEST(Merge, InstancesMergeIsStageError) {
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 2, length = 1.0)\n"
        "src = box(size = (1, 1, 1))\n"
        "inst = instance_on_points(l, src)\n"
        "m = merge(inst, inst)\n"
        "output m\n");
    EXPECT_EQ(countCode(r, "E201"), 1);
}

TEST(Merge, DetailConflictIsE609) {
    // Different per-anchor constants on detail: merging would silently keep
    // the left one (the stone_arch band trap) — now a hard error.
    pgg::RunResult r = pgg::run(
        "s1 = set(mesh_line(count = 1, length = 0.0), \"scale\", 2.0)\n"
        "s2 = set(mesh_line(count = 1, length = 0.0), \"scale\", 3.0)\n"
        "pts = merge(s1, s2)\n"
        "output pts\n");
    EXPECT_EQ(countCode(r, "E609"), 1);
}

TEST(Merge, EqualDetailValuesAreNotAConflict) {
    pgg::RunResult r = pgg::run(
        "s1 = set(mesh_line(count = 2, length = 1.0), \"scale\", 2.0)\n"
        "s2 = set(mesh_line(count = 3, length = 2.0), \"scale\", 2.0)\n"
        "pts = merge(s1, s2)\n"
        "total = sum_of(@scale, on = pts)\n"
        "output total\n");
    expectNoErrors(r);
    EXPECT_NEAR(pgg::asF32(r.outputs[0].value), 10.0f, 1e-6f);  // 2.0 over 5 points
}

TEST(Merge, MixedDomainsAreE609) {
    // Same value but different domains: the merge would zero-fill the detail
    // side's points and the fresh column would shadow its value on reads.
    pgg::RunResult r = pgg::run(
        "s1 = set(mesh_line(count = 1, length = 0.0), \"scale\", 2.0, domain = points)\n"
        "s2 = set(mesh_line(count = 1, length = 0.0), \"scale\", 2.0)\n"
        "pts = merge(s1, s2)\n"
        "output pts\n");
    EXPECT_EQ(countCode(r, "E609"), 1);
}

TEST(Merge, DetailGroupConflictIsE609) {
    pgg::RunResult r = pgg::run(
        "a = mark(box(size = (1, 1, 1)), \"tag\", where = true, domain = detail)\n"
        "b = mark(box(size = (1, 1, 1)), \"tag\", where = false, domain = detail)\n"
        "m = merge(a, b)\n"
        "output m\n");
    EXPECT_EQ(countCode(r, "E609"), 1);
}

TEST(Merge, PreservesStampsThroughInstancePipeline) {
    // merge of two point clouds keeps their per-anchor stamps for instancing.
    // Constants meant per point are written with domain = points — E609
    // refuses the silent detail left-wins this test used to unknowingly
    // exercise (every anchor was actually stamped x2, and the count-only
    // assertion could not see it).
    pgg::RunResult r = pgg::run(
        "l1 = mesh_line(count = 2, length = 1.0)\n"
        "l2 = mesh_line(count = 2, length = 1.0)\n"
        "s1 = set(l1, \"scale\", 2.0, domain = points)\n"
        "s2 = set(l2, \"scale\", 3.0, domain = points)\n"
        "pts = merge(s1, s2)\n"
        "src = box(size = (1, 1, 1))\n"
        "real = realize(instance_on_points(pts, src))\n"
        "output real\n");
    expectNoErrors(r);
    pgg::GeoPtr g = geoOutput(r, "real");
    ASSERT_TRUE(g);
    // 4 instances of a unit box: 2 scaled x2, 2 scaled x3 -> 32 verts, and the
    // realized union is 3 units tall (silent detail merge would give 2).
    EXPECT_EQ(g->pointCount(), 32u);
    float lo = 1e9f, hi = -1e9f;
    for (const glm::vec3& p : *g->positions) {
        lo = std::min(lo, p.y);
        hi = std::max(hi, p.y);
    }
    EXPECT_NEAR(hi - lo, 3.0f, 1e-5f);
}

// §19 v1.13: realize carries every source domain (face groups, corner N),
// rotating corner N with the stamp; transform composes instance stamps and
// rotates corner N.
TEST(Instance, RealizeCarriesFaceGroupsAndCornerNormals) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (2, 1, 1))\n"
        "t = mark(b, \"top\", where = dot(@N, (0, 1, 0)) > 0.5, domain = faces)\n"
        "f = compute_normals(t, mode = flat)\n"
        "l = mesh_line(count = 3, length = 6.0)\n"
        "o = set(l, \"orient\", orient_from_euler((0, 0, 90)))\n"
        "inst = instance_on_points(o, f)\n"
        "m = realize(inst)\n"
        "output m\n");
    expectNoErrors(r);
    pgg::GeoPtr m = geoOutput(r, "m");
    ASSERT_TRUE(m);
    EXPECT_EQ(m->pointCount(), 24u);
    EXPECT_EQ(m->faceCount(), 18u);
    ASSERT_TRUE(m->faceGroups);
    pgg::ConstBoolColumnPtr top = m->faceGroups->find("top");
    ASSERT_TRUE(top);
    ASSERT_EQ(top->size(), 18u);
    size_t inTop = 0;
    for (uint8_t v : *top) inTop += v ? 1 : 0;
    EXPECT_EQ(inTop, 3u);  // one top face per stamped box
    ASSERT_TRUE(m->cornerAttrs);
    const pgg::AttrColumn* n = m->cornerAttrs->find("N");
    ASSERT_TRUE(n);
    const auto& N = *std::get<std::shared_ptr<const std::vector<glm::vec3>>>(n->data);
    ASSERT_EQ(N.size(), 72u);
    // Corner N follows the stamp: roll 90 deg about Z sends +Y to -X, so the
    // top faces' corners now point along -X (and match the rotated geometry).
    for (size_t f = 0; f < m->faceCount(); ++f) {
        const glm::vec3 fn = glm::normalize(pgg::faceNormal(*m, f));
        for (int32_t c = (*m->faceOffsets)[f]; c < (*m->faceOffsets)[f + 1]; ++c) {
            const glm::vec3 cn = N[static_cast<size_t>(c)];
            if ((*top)[f]) EXPECT_NEAR(cn.x, -1.0f, 1e-5f);
            EXPECT_NEAR(glm::length(cn - fn), 0.0f, 1e-5f) << "face " << f;
        }
    }
    // Point attributes still materialize tint.
    ASSERT_TRUE(m->pointAttrs);
    EXPECT_TRUE(m->pointAttrs->find("tint"));
}

TEST(Instance, TransformRotatesCornerNormals) {
    pgg::RunResult r = pgg::run(
        "b = box(size = (2, 4, 6))\n"
        "f = compute_normals(b, mode = flat)\n"
        "t = transform(f, rotate = (0, 0, 90))\n"
        "output t\n");
    expectNoErrors(r);
    pgg::GeoPtr t = geoOutput(r, "t");
    ASSERT_TRUE(t && t->cornerAttrs);
    const pgg::AttrColumn* n = t->cornerAttrs->find("N");
    ASSERT_TRUE(n);
    const auto& N = *std::get<std::shared_ptr<const std::vector<glm::vec3>>>(n->data);
    ASSERT_EQ(N.size(), t->cornerCount());
    // Every corner normal must match its rotated face normal.
    for (size_t f = 0; f < t->faceCount(); ++f) {
        const glm::vec3 fn = glm::normalize(pgg::faceNormal(*t, f));
        for (int32_t c = (*t->faceOffsets)[f]; c < (*t->faceOffsets)[f + 1]; ++c)
            EXPECT_NEAR(glm::length(N[static_cast<size_t>(c)] - fn), 0.0f, 1e-5f) << "face " << f;
    }
}

TEST(Instance, TransformComposesInstanceStamps) {
    pgg::RunResult r = pgg::run(
        "l = mesh_line(count = 1, length = 1.0)\n"
        "b = box(size = (2, 1, 1))\n"
        "inst = instance_on_points(l, b)\n"
        "t = transform(inst, translate = (10, 0, 0), rotate = (0, 90, 0), scale = (2, 2, 2))\n"
        "m = realize(t)\n"
        "output m\n"
        "output t\n");
    expectNoErrors(r);
    pgg::GeoPtr t = geoOutput(r, "t");
    ASSERT_TRUE(t && t->pointAttrs);
    EXPECT_TRUE(t->pointAttrs->find("orient"));
    EXPECT_TRUE(t->pointAttrs->find("scale"));
    pgg::GeoPtr m = geoOutput(r, "m");
    ASSERT_TRUE(m);
    glm::vec3 mn, mx;
    pgg::geoBBox(*m, mn, mx);
    // The stamped box (long along X) is rotated 90 deg about Y (-> long along
    // Z), scaled x2 and its anchor moved to x = 10.
    EXPECT_NEAR(mn.x, 9.0f, 1e-4f);
    EXPECT_NEAR(mx.x, 11.0f, 1e-4f);
    EXPECT_NEAR(mn.z, -2.0f, 1e-4f);
    EXPECT_NEAR(mx.z, 2.0f, 1e-4f);
    EXPECT_NEAR(mx.y, 1.0f, 1e-4f);
}

}  // namespace
