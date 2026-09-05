// E4 acceptance tests (spec §15, §8.4): the first-class SDF type. Analytic
// probes of the pinned formulas (sphere/box/CSG/polynomial smin), the
// displace sample context (@P only, E307 with pass-through recovery), the
// instance transform mirroring realizeInstances with bit-exact AABB culling,
// marching cubes (watertight by edge-keyed dedup, thread-count invariant),
// the BVH voxelizer with the pseudo-sign, cache N3/N4 over sdf bindings and
// the e4_sdf_rock corpus goldens (both §15 criteria). SdfImprint/SdfGrind pin
// the masonry joint geometry (§19 v1.10): carve exactness, mutual vs
// one-sided slit widths, and the middle-surface grind partition.
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>

#include "pgg/eval.h"
#include "pgg/src/eval/cache.h"
#include "pgg/src/eval/sdf.h"
#include "test_utils.h"

namespace {

const std::string kCorpus = std::string(PGG_CORPUS_DIR) + "/e4_sdf_rock.pgg";

pgg::SdfPtr sdfOutput(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return pgg::asSdf(o.value);
    return nullptr;
}

bool hasDiag(const pgg::RunResult& r, const std::string& code) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code) return true;
    return false;
}

// Reference smin, written out per the pinned formula (spec §19 v1.2).
float sminRef(float a, float b, float k) {
    const float h = std::clamp(0.5f + 0.5f * (b - a) / k, 0.0f, 1.0f);
    return b + (a - b) * h - k * h * (1.0f - h);
}

// --- 1. analytic primitives ----------------------------------------------------

TEST(SdfAnalytic, SphereProbesAndBBox) {
    pgg::SdfPtr s = pgg::sdfSphere(2.0f);
    pggtest::expectF32Near(s->eval(glm::vec3(0, 0, 0)), -2.0f);
    pggtest::expectF32Near(s->eval(glm::vec3(2, 0, 0)), 0.0f);
    pggtest::expectF32Near(s->eval(glm::vec3(0, -2, 0)), 0.0f);
    pggtest::expectF32Near(s->eval(glm::vec3(4, 0, 0)), 2.0f);
    pggtest::expectF32Near(s->eval(glm::normalize(glm::vec3(1, 1, 1)) * 2.0f), 0.0f, 1e-5f);
    glm::vec3 mn, mx;
    s->conservativeBBox(mn, mx);
    pggtest::expectVec3Near(mn, glm::vec3(-2.0f));
    pggtest::expectVec3Near(mx, glm::vec3(2.0f));
}

TEST(SdfAnalytic, BoxProbesAndBBox) {
    pgg::SdfPtr b = pgg::sdfBox(glm::vec3(2, 4, 6));
    pggtest::expectF32Near(b->eval(glm::vec3(0, 0, 0)), -1.0f);  // min half-extent
    pggtest::expectF32Near(b->eval(glm::vec3(1, 0, 0)), 0.0f);   // face centers
    pggtest::expectF32Near(b->eval(glm::vec3(0, 2, 0)), 0.0f);
    pggtest::expectF32Near(b->eval(glm::vec3(0, 0, 3)), 0.0f);
    pggtest::expectF32Near(b->eval(glm::vec3(1, 2, 0)), 0.0f);   // edge midpoint
    pggtest::expectF32Near(b->eval(glm::vec3(1, 2, 3)), 0.0f);   // corner
    pggtest::expectF32Near(b->eval(glm::vec3(2, 0, 0)), 1.0f);
    pggtest::expectF32Near(b->eval(glm::vec3(2, 3, 4)), std::sqrt(3.0f), 1e-5f);
    glm::vec3 mn, mx;
    b->conservativeBBox(mn, mx);
    pggtest::expectVec3Near(mn, glm::vec3(-1, -2, -3));
    pggtest::expectVec3Near(mx, glm::vec3(1, 2, 3));
}

// --- 2. CSG ----------------------------------------------------------------------

TEST(SdfCsg, HardOpsMatchMinMax) {
    pgg::SdfPtr a = pgg::sdfSphere(1.0f);
    pgg::SdfPtr b = pgg::sdfBox(glm::vec3(1, 1, 1));
    const glm::vec3 p1(0.6f, 0, 0);     // a = -0.4, b = 0.1
    const glm::vec3 p2(0.9f, 0.9f, 0);  // a = sqrt(1.62)-1 ~= 0.2728, b = 0.4
    pgg::SdfPtr u = pgg::sdfUnion(a, b);
    pggtest::expectF32Near(u->eval(p1), -0.4f);
    pggtest::expectF32Near(u->eval(p2), std::sqrt(1.62f) - 1.0f, 1e-5f);
    pgg::SdfPtr s = pgg::sdfSubtract(a, b);
    pggtest::expectF32Near(s->eval(p1), -0.1f);  // max(a, -b)
    pggtest::expectF32Near(s->eval(p2), std::sqrt(1.62f) - 1.0f, 1e-5f);
    pgg::SdfPtr i = pgg::sdfIntersect(a, b);
    pggtest::expectF32Near(i->eval(p1), 0.1f);
    pggtest::expectF32Near(i->eval(glm::vec3(0.4f, 0, 0)), -0.1f);
}

TEST(SdfCsg, SmoothOpsMatchHandSmin) {
    pgg::SdfPtr a = pgg::sdfSphere(1.0f);
    pgg::SdfPtr b = pgg::sdfBox(glm::vec3(1, 1, 1));
    const float k = 0.6f;
    pgg::SdfPtr us = pgg::sdfUnionSmooth(a, b, k);
    pgg::SdfPtr ss = pgg::sdfSubtractSmooth(a, b, k);
    for (const glm::vec3 p : {glm::vec3(0.5f, 0, 0), glm::vec3(0.75f, 0.55f, 0), glm::vec3(1.3f, 0.2f, 0.1f),
                              glm::vec3(0.2f, 0.3f, 0.4f), glm::vec3(-0.8f, 0.1f, 0)}) {
        const float da = a->eval(p);
        const float db = b->eval(p);
        EXPECT_TRUE(std::isfinite(da));
        EXPECT_TRUE(std::isfinite(db));
        pggtest::expectF32Near(us->eval(p), sminRef(da, db, k), 1e-5f);
        // subtract_smooth(a,b,k) = smax(a,-b,k) = -smin(-a,b,k)
        pggtest::expectF32Near(ss->eval(p), -sminRef(-da, db, k), 1e-5f);
    }
}

TEST(SdfCsg, DegenerateInputsStayFinite) {
    // k <= 0 falls back to the hard op (no 0/0); +inf is the smin identity
    // (empty instance field unioned in).
    pgg::SdfPtr a = pgg::sdfSphere(1.0f);
    pgg::SdfPtr b = pgg::sdfBox(glm::vec3(1, 1, 1));
    pgg::SdfPtr z = pgg::sdfUnionSmooth(a, b, 0.0f);
    pggtest::expectF32Near(z->eval(glm::vec3(0.6f, 0, 0)), -0.4f);
    pgg::SdfPtr empty = pgg::sdfInstance(pgg::sdfSphere(0.5f), {}, 0.3f);
    pgg::SdfPtr u = pgg::sdfUnionSmooth(a, empty, 0.3f);
    pggtest::expectF32Near(u->eval(glm::vec3(0.6f, 0, 0)), -0.4f);
    pgg::SdfPtr s = pgg::sdfSubtractSmooth(a, empty, 0.3f);
    pggtest::expectF32Near(s->eval(glm::vec3(0.6f, 0, 0)), -0.4f);
    for (const glm::vec3 p : {glm::vec3(0, 0, 0), glm::vec3(1, 1, 1), glm::vec3(-2, 0.5f, 0)})
        EXPECT_TRUE(std::isfinite(u->eval(p)));
}

// --- 3. displace -----------------------------------------------------------------

TEST(SdfDisplace, ConstantAmountShiftsIso) {
    pgg::RunResult r = pgg::run(
        "s = sdf_displace(sdf_sphere(r = 1.0), amount = 0.25)\n"
        "output s\n");
    pggtest::expectNoErrors(r);
    pgg::SdfPtr s = sdfOutput(r, "s");
    ASSERT_TRUE(s);
    pggtest::expectF32Near(s->eval(glm::vec3(1, 0, 0)), 0.25f);   // d = child + amount
    pggtest::expectF32Near(s->eval(glm::vec3(0.75f, 0, 0)), 0.0f, 1e-5f);
}

TEST(SdfDisplace, PositionDependentAmount) {
    pgg::RunResult r = pgg::run(
        "s = sdf_displace(sdf_sphere(r = 1.0), amount = dot(@P, (1, 0, 0)) * 0.1)\n"
        "output s\n");
    pggtest::expectNoErrors(r);
    pgg::SdfPtr s = sdfOutput(r, "s");
    ASSERT_TRUE(s);
    pggtest::expectF32Near(s->eval(glm::vec3(1, 0, 0)), 0.1f);
    pggtest::expectF32Near(s->eval(glm::vec3(-1, 0, 0)), -0.1f);
    pggtest::expectF32Near(s->eval(glm::vec3(0, 0, 1)), 0.0f, 1e-6f);
}

TEST(SdfDisplace, FbmAmountIsDeterministicAndShiftsField) {
    const char* src =
        "root_rng = rng_from_seed(7)\n"
        "n_rng = split_rng(root_rng, key = \"n\")\n"
        "s = sdf_displace(sdf_sphere(r = 1.0), amount = fbm(scale = 2.0, octaves = 3, rng = n_rng) * 0.2)\n"
        "output s\n";
    pgg::RunResult a = pgg::run(src);
    pgg::RunResult b = pgg::run(src);
    pggtest::expectNoErrors(a);
    pggtest::expectNoErrors(b);
    pgg::SdfPtr sa = sdfOutput(a, "s");
    pgg::SdfPtr sb = sdfOutput(b, "s");
    ASSERT_TRUE(sa);
    ASSERT_TRUE(sb);
    bool anyShift = false;
    for (const glm::vec3 p : {glm::vec3(1, 0, 0), glm::vec3(0.3f, 0.8f, 0.4f), glm::vec3(-0.9f, 0.2f, 0.3f)}) {
        EXPECT_EQ(sa->eval(p), sb->eval(p));  // bit-identical across runs
        anyShift = anyShift || sa->eval(p) != glm::length(p) - 1.0f;
    }
    EXPECT_TRUE(anyShift);
}

TEST(SdfDisplace, SampleContextViolationIsE307WithPassThrough) {
    // @N/@index/@attribute/distance_to/ingroup are not evaluable in the SDF
    // sample context: E307, and the recovery passes the child through (the
    // output sdf evaluates exactly like the undisplaced sphere).
    const char* bad[] = {
        "dot(@N, (0, 1, 0))",
        "@index",
        "@foo",
        "distance_to(ico_sphere(subdiv = 1, radius = 1.0))",
        "ingroup(\"g\")",
    };
    for (const char* field : bad) {
        const std::string src = std::string("s = sdf_displace(sdf_sphere(r = 1.0), amount = ") + field + ")\n"
                                "output s\n";
        pgg::RunResult r = pgg::run(src);
        EXPECT_TRUE(hasDiag(r, "E307")) << field;
        pgg::SdfPtr s = sdfOutput(r, "s");
        ASSERT_TRUE(s) << field;
        for (const glm::vec3 p : {glm::vec3(1, 0, 0), glm::vec3(0.2f, 0.7f, 0.5f)})
            EXPECT_NEAR(s->eval(p), glm::length(p) - 1.0f, 1e-5f) << field;
    }
}

// --- 4. instance ------------------------------------------------------------------

pgg::SdfInstanceAnchor anchorAt(glm::vec3 pos, float scale = 1.0f,
                                glm::quat orient = glm::quat(1.0f, 0.0f, 0.0f, 0.0f)) {
    pgg::SdfInstanceAnchor an;
    an.pos = pos;
    an.scale = scale;
    an.orient = orient;
    return an;
}

TEST(SdfInstance, TwoAnchorsHardUnion) {
    pgg::SdfPtr s = pgg::sdfInstance(pgg::sdfSphere(1.0f),
                                     {anchorAt(glm::vec3(0, 0, 0)), anchorAt(glm::vec3(3, 0, 0))}, 0.0f);
    pggtest::expectF32Near(s->eval(glm::vec3(0.5f, 0, 0)), -0.5f);
    pggtest::expectF32Near(s->eval(glm::vec3(2.5f, 0, 0)), -0.5f);
    pggtest::expectF32Near(s->eval(glm::vec3(1.5f, 0, 0)), 0.5f);
    pggtest::expectF32Near(s->eval(glm::vec3(5, 0, 0)), 1.0f);
    glm::vec3 mn, mx;
    s->conservativeBBox(mn, mx);
    pggtest::expectVec3Near(mn, glm::vec3(-1, -1, -1));
    pggtest::expectVec3Near(mx, glm::vec3(4, 1, 1));
}

TEST(SdfInstance, SmoothBlendMatchesHandSmin) {
    pgg::SdfPtr src = pgg::sdfSphere(1.0f);
    const float k = 0.6f;
    pgg::SdfPtr s = pgg::sdfInstance(src, {anchorAt(glm::vec3(0, 0, 0)), anchorAt(glm::vec3(1.2f, 0, 0))}, k);
    // Midpoint: both anchors evaluate to -0.4; the smin dip sinks below that.
    const glm::vec3 mid(0.6f, 0, 0);
    pggtest::expectF32Near(s->eval(mid), sminRef(-0.4f, -0.4f, k), 1e-5f);
    EXPECT_LT(s->eval(mid), -0.4f);  // blend bulge below the hard min
    // Reduction is in anchor order: far points still evaluate exactly.
    pggtest::expectF32Near(s->eval(glm::vec3(2.2f, 0, 0)), 0.0f, 1e-5f);
}

TEST(SdfInstance, ScaleAndOrientStampMirrorsRealize) {
    // T(@P)*R(@orient)*S(@scale): local = q^-1*(p - P)/s, d = source(local)*s.
    pgg::SdfPtr scaled = pgg::sdfInstance(pgg::sdfSphere(1.0f), {anchorAt(glm::vec3(0, 0, 0), 2.0f)}, 0.0f);
    pggtest::expectF32Near(scaled->eval(glm::vec3(1.5f, 0, 0)), -0.5f);
    pggtest::expectF32Near(scaled->eval(glm::vec3(2, 0, 0)), 0.0f, 1e-5f);

    const glm::quat qz90 = glm::normalize(glm::quat(glm::radians(glm::vec3(0, 0, 90))));
    pgg::SdfPtr rotated = pgg::sdfInstance(pgg::sdfBox(glm::vec3(2, 0.2f, 0.2f)),
                                           {anchorAt(glm::vec3(0, 0, 0), 1.0f, qz90)}, 0.0f);
    // The box's long axis now points along +y.
    pggtest::expectF32Near(rotated->eval(glm::vec3(0, 0.9f, 0)), -0.1f);
    pggtest::expectF32Near(rotated->eval(glm::vec3(0.15f, 0, 0)), 0.05f);
}

TEST(SdfInstance, AabbCullingIsBitExact) {
    // A far anchor is culled by its world AABB: evals with and without it are
    // bit-identical (smin(a,b,k) == a exactly when b-a >= k).
    pgg::SdfPtr src = pgg::sdfSphere(1.0f);
    const float k = 0.4f;
    pgg::SdfPtr one = pgg::sdfInstance(src, {anchorAt(glm::vec3(0, 0, 0))}, k);
    pgg::SdfPtr two = pgg::sdfInstance(src, {anchorAt(glm::vec3(0, 0, 0)), anchorAt(glm::vec3(100, 0, 0))}, k);
    for (int i = 0; i <= 8; ++i)
        for (int j = 0; j <= 8; ++j)
            for (int l = 0; l <= 8; ++l) {
                const glm::vec3 p(-2.0f + 0.5f * i, -2.0f + 0.5f * j, -2.0f + 0.5f * l);
                EXPECT_EQ(one->eval(p), two->eval(p)) << p.x << " " << p.y << " " << p.z;
            }
    // Sanity: a near anchor does change the field (culling does not over-skip).
    pgg::SdfPtr near = pgg::sdfInstance(src, {anchorAt(glm::vec3(0, 0, 0)), anchorAt(glm::vec3(1.5f, 0, 0))}, k);
    EXPECT_NE(one->eval(glm::vec3(1.5f, 0, 0)), near->eval(glm::vec3(1.5f, 0, 0)));
}

TEST(SdfInstance, StampsReadThroughRun) {
    pgg::RunResult r = pgg::run(
        "rng = rng_from_seed(11)\n"
        "pts0 = mesh_line(count = 2, length = 4.0, dir = (1, 0, 0))\n"
        "pts1 = set(pts0, \"scale\", random(lo = 0.5, hi = 0.5, rng = rng))\n"
        "pts = set(pts1, \"orient\", orient_from_euler((0, 0, 90)))\n"
        "s = sdf_instance_on_points(pts, sdf_box(size = (2, 0.2, 0.2)))\n"
        "output s\n");
    pggtest::expectNoErrors(r);
    pgg::SdfPtr s = sdfOutput(r, "s");
    ASSERT_TRUE(s);
    // scale 0.5 and a 90-degree z rotation: the long axis lies along y.
    pggtest::expectF32Near(s->eval(glm::vec3(0, 0.4f, 0)), -0.05f);
    pggtest::expectF32Near(s->eval(glm::vec3(0.2f, 0, 0)), 0.15f);
}

// --- 5. mesh_from_sdf --------------------------------------------------------------

TEST(MeshFromSdf, SphereIsWatertightWithGoldenCounts) {
    pgg::RunResult r = pgg::run(
        "m = mesh_from_sdf(sdf_sphere(r = 1.0), voxel = 0.05)\n"
        "output m\n");
    pggtest::expectNoErrors(r);
    pgg::GeoPtr m = pggtest::geoOutput(r, "m");
    ASSERT_TRUE(m);
    // Golden counts of the current numeric profile.
    EXPECT_EQ(m->pointCount(), 7506u);
    EXPECT_EQ(m->cornerCount(), 45024u);
    EXPECT_EQ(m->faceCount(), 15008u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*m), 0u);
    glm::vec3 mn, mx;
    pgg::geoBBox(*m, mn, mx);
    pggtest::expectVec3Near(mn, glm::vec3(-1, -1, -1), 1e-4f);
    pggtest::expectVec3Near(mx, glm::vec3(1, 1, 1), 1e-4f);
    // Attribute barrier: only @P (normals come from compute_normals).
    EXPECT_FALSE(m->normals);
    EXPECT_FALSE(m->pointAttrs);
    EXPECT_FALSE(m->pointGroups);
}

TEST(MeshFromSdf, IsoShiftMovesSurface) {
    // The extraction level: {field = iso}; for a sphere field |p|-r a negative
    // iso pulls the surface inward to r + iso.
    pgg::RunResult r = pgg::run(
        "m = mesh_from_sdf(sdf_sphere(r = 1.0), voxel = 0.05, iso = -0.3)\n"
        "output m\n");
    pggtest::expectNoErrors(r);
    pgg::GeoPtr m = pggtest::geoOutput(r, "m");
    ASSERT_TRUE(m);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*m), 0u);
    glm::vec3 mn, mx;
    pgg::geoBBox(*m, mn, mx);
    pggtest::expectVec3Near(mn, glm::vec3(-0.7f, -0.7f, -0.7f), 1e-4f);
    pggtest::expectVec3Near(mx, glm::vec3(0.7f, 0.7f, 0.7f), 1e-4f);
    EXPECT_LT(m->pointCount(), 7506u);  // smaller sphere -> fewer vertices
}

TEST(MeshFromSdf, ThreadCountInvariant) {
    // 43^3 lattice (>= 4096 samples): the grid sampling takes the pool path;
    // results must be bit-identical (N7).
    const char* src =
        "root_rng = rng_from_seed(3)\n"
        "n_rng = split_rng(root_rng, key = \"n\")\n"
        "s = sdf_displace(sdf_sphere(r = 1.0), amount = fbm(scale = 2.5, octaves = 3, rng = n_rng) * 0.1)\n"
        "m = mesh_from_sdf(s, voxel = 0.05)\n"
        "output m\n";
    pgg::RunParams p1;
    p1.threads = 1;
    pgg::RunResult r1 = pgg::run(src, p1);
    pgg::RunParams p8;
    p8.threads = 8;
    pgg::RunResult r8 = pgg::run(src, p8);
    pggtest::expectNoErrors(r1);
    pggtest::expectNoErrors(r8);
    EXPECT_EQ(r1.stats.threadsUsed, 1u);
    EXPECT_EQ(r8.stats.threadsUsed, 8u);
    EXPECT_EQ(pggtest::geoContentHash(pggtest::geoOutput(r1, "m")),
              pggtest::geoContentHash(pggtest::geoOutput(r8, "m")));
}

TEST(MeshFromSdf, AxisGuardIsE306) {
    pgg::RunResult r = pgg::run(
        "m = mesh_from_sdf(sdf_sphere(r = 1.0), voxel = 0.0001)\n"
        "output m\n");
    EXPECT_TRUE(hasDiag(r, "E306"));
    pgg::GeoPtr m = pggtest::geoOutput(r, "m");
    ASSERT_TRUE(m);
    EXPECT_EQ(m->pointCount(), 0u);
}

TEST(MeshFromSdf, BoundarySurfaceTriggersW002) {
    // The displace amplitude is estimated on a 4^3 probe of the child bbox;
    // this narrow spike (support 0.3 at (1,0,0)) is invisible to that probe,
    // so the estimated bbox misses the bulge and the surface reaches the grid
    // boundary slab -> W002 (extraction itself succeeds).
    pgg::RunResult r = pgg::run(
        "s = sdf_displace(sdf_sphere(r = 1.0), amount = -0.8 * max(0.0, 1.0 - length(@P - (1, 0, 0)) / 0.3))\n"
        "m = mesh_from_sdf(s, voxel = 0.05)\n"
        "output m\n");
    EXPECT_FALSE(r.hasErrors());
    EXPECT_TRUE(hasDiag(r, "W002"));
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == "W002") EXPECT_TRUE(d.isWarning);
    pgg::GeoPtr m = pggtest::geoOutput(r, "m");
    ASSERT_TRUE(m);
    EXPECT_GT(m->pointCount(), 0u);
}

TEST(MeshFromSdf, BoundaryDetectorDirect) {
    // Direct detector unit: a Grid node whose boundary slab goes below iso.
    auto grid = [](float fill) {
        auto g = std::make_shared<pgg::SdfNode>();
        g->kind = pgg::SdfKind::Grid;
        g->voxel = 1.0f;
        g->origin = glm::vec3(0, 0, 0);
        g->dims = glm::ivec3(2);
        g->values = std::make_shared<const std::vector<float>>(8, fill);
        return g;
    };
    pgg::MeshFromSdfResult clean = pgg::meshFromSdfExtract(*grid(1.0f), 1.0f, 0.0f, 1);
    EXPECT_FALSE(clean.boundaryTouch);
    pgg::MeshFromSdfResult touched = pgg::meshFromSdfExtract(*grid(-1.0f), 1.0f, 0.0f, 1);
    EXPECT_TRUE(touched.boundaryTouch);
}

// --- 6. sdf_from_mesh ----------------------------------------------------------------

TEST(SdfFromMesh, IcoSphereProbes) {
    pgg::RunResult r = pgg::run(
        "s = sdf_from_mesh(ico_sphere(subdiv = 2, radius = 1.0), voxel = 0.05)\n"
        "output s\n");
    pggtest::expectNoErrors(r);
    pgg::SdfPtr s = sdfOutput(r, "s");
    ASSERT_TRUE(s);
    EXPECT_GT(s->eval(glm::vec3(2, 0, 0)), 0.9f);   // outside
    EXPECT_LT(s->eval(glm::vec3(2, 0, 0)), 1.1f);
    EXPECT_LT(s->eval(glm::vec3(0, 0, 0)), -0.9f);  // inside
    EXPECT_GT(s->eval(glm::vec3(0, 0, 0)), -1.1f);
    for (const glm::vec3 d : {glm::vec3(1, 0, 0), glm::vec3(0, 1, 0), glm::vec3(0, 0, 1),
                              glm::normalize(glm::vec3(1, 1, 1))}) {
        EXPECT_LT(std::fabs(s->eval(d * 1.02f)), 0.1f);  // near the surface
    }
}

TEST(SdfFromMesh, RoundTripIsWatertightAndClose) {
    pgg::RunResult r = pgg::run(
        "s = sdf_from_mesh(ico_sphere(subdiv = 2, radius = 1.0), voxel = 0.05)\n"
        "m = mesh_from_sdf(s, voxel = 0.08)\n"
        "output m\n");
    pggtest::expectNoErrors(r);
    pgg::GeoPtr m = pggtest::geoOutput(r, "m");
    ASSERT_TRUE(m);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*m), 0u);
    glm::vec3 mn, mx;
    pgg::geoBBox(*m, mn, mx);
    for (int i = 0; i < 3; ++i) {
        EXPECT_NEAR(mn[i], -1.0f, 0.15f);
        EXPECT_NEAR(mx[i], 1.0f, 0.15f);
    }
}

TEST(SdfFromMesh, VoxelizationIsDeterministicAcrossThreads) {
    const char* src =
        "s = sdf_from_mesh(ico_sphere(subdiv = 2, radius = 1.0), voxel = 0.05)\n"
        "output s\n";
    pgg::RunParams p1;
    p1.threads = 1;
    pgg::RunResult r1 = pgg::run(src, p1);
    pgg::RunParams p8;
    p8.threads = 8;
    pgg::RunResult r8 = pgg::run(src, p8);
    pggtest::expectNoErrors(r1);
    pggtest::expectNoErrors(r8);
    pgg::SdfPtr s1 = sdfOutput(r1, "s");
    pgg::SdfPtr s8 = sdfOutput(r8, "s");
    ASSERT_TRUE(s1);
    ASSERT_TRUE(s8);
    for (int i = 0; i <= 8; ++i)
        for (int j = 0; j <= 8; ++j) {
            const glm::vec3 p(-1.5f + 0.375f * i, -1.5f + 0.375f * j, 0.31f * (i - j));
            EXPECT_EQ(s1->eval(p), s8->eval(p));
        }
}

// --- 7. cache (N3/N4 over sdf bindings) ----------------------------------------------

const char* kCachedSdfSrc =
    "root_rng = rng_from_seed(1)\n"
    "n_rng = split_rng(root_rng, key = \"n\")\n"
    "s = sdf_displace(sdf_sphere(r = 1.0), amount = fbm(scale = 2.0, rng = n_rng) * 0.1)\n"
    "m = mesh_from_sdf(s, voxel = 0.1)\n"
    "output s\n"
    "output m\n";

TEST(SdfCache, SecondRunHitsSdfAndMeshBindings) {
    pgg::MemoryCache cache;
    pgg::RunParams p;
    p.cache = &cache;
    pgg::RunResult r1 = pgg::run(std::string(kCachedSdfSrc), p);
    pggtest::expectNoErrors(r1);
    EXPECT_EQ(r1.stats.cacheHits, 0u);
    EXPECT_EQ(r1.stats.cacheMisses, 4u);  // s, m and the two rng bindings (never stored)

    pgg::RunResult r2 = pgg::run(std::string(kCachedSdfSrc), p);
    pggtest::expectNoErrors(r2);
    EXPECT_EQ(r2.stats.cacheHits, 2u);    // both output roots hit (sdf and mesh)
    EXPECT_EQ(r2.stats.cacheMisses, 0u);  // a hit short-circuits the whole subgraph
    EXPECT_EQ(r2.stats.fieldsEvaluated, 0u);
    EXPECT_EQ(*pggtest::geoOutput(r1, "m")->positions, *pggtest::geoOutput(r2, "m")->positions);
    EXPECT_EQ(sdfOutput(r1, "s")->eval(glm::vec3(0.3f, 0.8f, 0.4f)),
              sdfOutput(r2, "s")->eval(glm::vec3(0.3f, 0.8f, 0.4f)));
}

TEST(SdfCache, VoxelEditInvalidatesOnlyTheMeshBinding) {
    pgg::MemoryCache cache;
    pgg::RunParams p;
    p.cache = &cache;
    pgg::RunResult r1 = pgg::run(std::string(kCachedSdfSrc), p);
    pggtest::expectNoErrors(r1);

    const std::string edited =
        "root_rng = rng_from_seed(1)\n"
        "n_rng = split_rng(root_rng, key = \"n\")\n"
        "s = sdf_displace(sdf_sphere(r = 1.0), amount = fbm(scale = 2.0, rng = n_rng) * 0.1)\n"
        "m = mesh_from_sdf(s, voxel = 0.2)\n"  // only this knob changed
        "output s\n"
        "output m\n";
    pgg::RunResult r2 = pgg::run(edited, p);
    pggtest::expectNoErrors(r2);
    EXPECT_EQ(r2.stats.cacheHits, 1u);    // s hits; its owned displace field survives the cache
    EXPECT_EQ(r2.stats.cacheMisses, 1u);  // only m recomputes
    // The mesh recomputed from the cached sdf payload equals a fresh run.
    pgg::RunResult fresh = pgg::run(edited);
    pggtest::expectNoErrors(fresh);
    EXPECT_EQ(pggtest::geoContentHash(pggtest::geoOutput(r2, "m")),
              pggtest::geoContentHash(pggtest::geoOutput(fresh, "m")));
}

// --- 8. corpus (both acceptance criteria) ---------------------------------------------

TEST(SdfCorpus, RockAndWallMatchGoldens) {
    pgg::RunParams p;
    p.threads = 1;
    pgg::RunResult r = pgg::runFile(kCorpus, p);
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.outputs.size(), 3u);

    // Criterion 1: the SDF-pipeline rock (exact counts of the current profile).
    pgg::GeoPtr rock = pggtest::geoOutput(r, "rock");
    ASSERT_TRUE(rock);
    EXPECT_EQ(rock->pointCount(), 12508u);
    EXPECT_EQ(rock->cornerCount(), 75012u);
    EXPECT_EQ(rock->faceCount(), 25004u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*rock), 0u);
    glm::vec3 rmn, rmx;
    pgg::geoBBox(*rock, rmn, rmx);
    pggtest::expectVec3Near(rmn, glm::vec3(-1.20845f, -1.23195f, -1.24334f), 1e-4f);
    pggtest::expectVec3Near(rmx, glm::vec3(1.24725f, 1.24194f, 1.23437f), 1e-4f);
    ASSERT_TRUE(rock->normals);  // compute_normals ran after extraction

    // Criterion 2: the instance wall.
    pgg::GeoPtr wall = pggtest::geoOutput(r, "wall");
    ASSERT_TRUE(wall);
    EXPECT_EQ(wall->pointCount(), 13542u);
    EXPECT_EQ(wall->cornerCount(), 81168u);
    EXPECT_EQ(wall->faceCount(), 27056u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*wall), 0u);
    glm::vec3 wmn, wmx;
    pgg::geoBBox(*wall, wmn, wmx);
    pggtest::expectVec3Near(wmn, glm::vec3(-0.572802f, -0.603777f, 3.16101f), 1e-4f);
    pggtest::expectVec3Near(wmx, glm::vec3(10.7438f, 0.604746f, 4.83796f), 1e-4f);

    // The raw sdf output (§6.7 allows sdf roots; PggTool prints its summary).
    pgg::SdfPtr field = sdfOutput(r, "rock_field");
    ASSERT_TRUE(field);
    EXPECT_EQ(pgg::sdfNodeCount(*field), 7u);
    glm::vec3 fmn, fmx;
    field->conservativeBBox(fmn, fmx);
    pggtest::expectVec3Near(fmn, glm::vec3(-1.79265f), 1e-4f);
    pggtest::expectVec3Near(fmx, glm::vec3(1.79265f), 1e-4f);
}

TEST(SdfCorpus, ThreadCountInvariant) {
    pgg::RunParams p1;
    p1.threads = 1;
    pgg::RunResult r1 = pgg::runFile(kCorpus, p1);
    pgg::RunParams p8;
    p8.threads = 8;
    pgg::RunResult r8 = pgg::runFile(kCorpus, p8);
    pggtest::expectNoErrors(r1);
    pggtest::expectNoErrors(r8);
    for (const char* out : {"rock", "wall"}) {
        EXPECT_EQ(pggtest::geoContentHash(pggtest::geoOutput(r1, out)),
                  pggtest::geoContentHash(pggtest::geoOutput(r8, out))) << out;
    }
    // The sdf payload itself is thread-invariant too.
    pgg::SdfPtr f1 = sdfOutput(r1, "rock_field");
    pgg::SdfPtr f8 = sdfOutput(r8, "rock_field");
    ASSERT_TRUE(f1);
    ASSERT_TRUE(f8);
    for (const glm::vec3 p : {glm::vec3(0.5f, 0.2f, -0.7f), glm::vec3(1.1f, -0.4f, 0.3f)})
        EXPECT_EQ(f1->eval(p), f8->eval(p));
}

// --- 9. imprint carve geometry ---------------------------------------------------
// The masonry corpus examples (stone_row, stone_arch) fit stones by carving
// them with the NEIGHBOUR'S INFLATED field (sdf_subtract(A, sdf_displace(B,
// -gap))). These tests pin the geometry of that operation: the carve stops
// exactly at the neighbour's inflated surface (mechanism is exact), and the
// MUTUAL carve of an overlapping pair produces a slit of width overlap + 2g
// (the designed overlap is converted into visible seam), while the one-sided
// carve yields a uniform g-wide seam regardless of overlap — which is why the
// corpus carves only the newly placed stone of each pair.

// Sign-change crossing of f along a ray from the origin (50 bisections,
// ~1e-9). xa and xb must straddle the crossing (either order).
float crossOnRay(const pgg::SdfPtr& f, glm::vec3 dir, float ta, float tb) {
    float fa = f->eval(dir * ta);
    float fb = f->eval(dir * tb);
    EXPECT_LT(fa, 0.0f);
    EXPECT_GT(fb, 0.0f);
    for (int i = 0; i < 50; ++i) {
        const float tm = 0.5f * (ta + tb);
        if (f->eval(dir * tm) < 0.0f)
            ta = tm;
        else
            tb = tm;
    }
    return 0.5f * (ta + tb);
}

TEST(SdfImprint, CarveBoundaryMatchesInflatedSurfaceExactly) {
    // Unit spheres A at origin and B at (1.8, 0, 0) (overlap 0.2). Carving A
    // by B inflated by g = 0.05 must cut A at B's inflated surface
    // x = 1.8 - 1 - 0.05 = 0.75 — no deeper, no shallower.
    pgg::RunResult r = pgg::run(
        "a = sdf_sphere(r = 1.0)\n"
        "anchor = transform(mesh_line(count = 1, length = 0.0), translate = (1.8, 0, 0))\n"
        "b = sdf_instance_on_points(anchor, source = sdf_sphere(r = 1.0))\n"
        "carved = sdf_subtract(a, sdf_displace(b, -0.05))\n"
        "output carved\n"
        "output b\n");
    pggtest::expectNoErrors(r);
    pgg::SdfPtr carved = sdfOutput(r, "carved");
    pgg::SdfPtr b = sdfOutput(r, "b");
    ASSERT_TRUE(carved);
    ASSERT_TRUE(b);
    pggtest::expectF32Near(crossOnRay(carved, glm::vec3(1, 0, 0), 0.5f, 1.0f), 0.75f, 1e-4f);
    pggtest::expectF32Near(crossOnRay(b, glm::vec3(1, 0, 0), 1.0f, 0.5f), 0.8f, 1e-5f);
    // The mechanism is exact off-axis too (crossing circle of the two
    // spheres: x = 0.9, |y| = sqrt(1 - 0.81) ~ 0.4359; carve depth along this
    // ray = dist to B's surface + g).
    const glm::vec3 dir = glm::normalize(glm::vec3(0.9f, 0.4359f, 0.0f));
    const float t = crossOnRay(carved, dir, 0.5f, 1.0f);
    pggtest::expectF32Near(b->eval(dir * t), 0.05f, 1e-4f);  // carve iso == B + g
}

TEST(SdfImprint, CarveSurfaceFollowsDisplacedNeighbour) {
    // Same pair, but B's surface is fbm-roughened (pseudo-SDF, as in
    // boulder_field). The carved iso of A must still BE the inflated iso of B
    // wherever it cuts: b(carve point) == g on every probe ray.
    const char* src =
        "root_rng = rng_from_seed(11)\n"
        "n_rng = split_rng(root_rng, key = \"n\")\n"
        "a = sdf_sphere(r = 1.0)\n"
        "anchor = transform(mesh_line(count = 1, length = 0.0), translate = (1.8, 0, 0))\n"
        "b = sdf_instance_on_points(anchor, source = sdf_displace(sdf_sphere(r = 1.0), amount = fbm(scale = 2.0, octaves = 3, rng = n_rng) * 0.04))\n"
        "carved = sdf_subtract(a, sdf_displace(b, -0.05))\n"
        "output carved\n"
        "output b\n";
    pgg::RunResult r = pgg::run(src);
    pggtest::expectNoErrors(r);
    pgg::SdfPtr carved = sdfOutput(r, "carved");
    pgg::SdfPtr b = sdfOutput(r, "b");
    ASSERT_TRUE(carved);
    ASSERT_TRUE(b);
    for (const glm::vec3 dir : {glm::normalize(glm::vec3(1, 0.3f, 0.1f)), glm::normalize(glm::vec3(1, -0.2f, 0.35f)),
                                glm::normalize(glm::vec3(1, 0.1f, -0.3f))}) {
        SCOPED_TRACE("ray " + std::to_string(dir.x) + " " + std::to_string(dir.y) + " " + std::to_string(dir.z));
        const float t = crossOnRay(carved, dir, 0.4f, 1.05f);
        pggtest::expectF32Near(b->eval(dir * t), 0.05f, 1e-4f);
    }
}

TEST(SdfImprint, MutualCarveWidensSeamByOverlap) {
    // Same pair (overlap 0.2, g = 0.05). MUTUAL carve: A' ends at 0.75 and
    // B' starts at 1.05 → slit 0.30 = overlap + 2g. ONE-SIDED carve (only the
    // new stone A is cut): slit 0.05 = g, hugging B's untouched surface.
    pgg::RunResult r = pgg::run(
        "a = sdf_sphere(r = 1.0)\n"
        "anchor = transform(mesh_line(count = 1, length = 0.0), translate = (1.8, 0, 0))\n"
        "b = sdf_instance_on_points(anchor, source = sdf_sphere(r = 1.0))\n"
        "mut_a = sdf_subtract(a, sdf_displace(b, -0.05))\n"
        "mut_b = sdf_subtract(b, sdf_displace(a, -0.05))\n"
        "output mut_a\n"
        "output mut_b\n"
        "output b\n");
    pggtest::expectNoErrors(r);
    pgg::SdfPtr mutA = sdfOutput(r, "mut_a");
    pgg::SdfPtr mutB = sdfOutput(r, "mut_b");
    pgg::SdfPtr b = sdfOutput(r, "b");
    ASSERT_TRUE(mutA);
    ASSERT_TRUE(mutB);
    ASSERT_TRUE(b);
    const float aEnd = crossOnRay(mutA, glm::vec3(1, 0, 0), 0.5f, 1.0f);
    const float mutBStart = crossOnRay(mutB, glm::vec3(1, 0, 0), 1.3f, 0.9f);
    const float bStart = crossOnRay(b, glm::vec3(1, 0, 0), 1.0f, 0.5f);
    pggtest::expectF32Near(aEnd, 0.75f, 1e-4f);
    pggtest::expectF32Near(mutBStart, 1.05f, 1e-4f);
    pggtest::expectF32Near(mutBStart - aEnd, 0.2f + 2.0f * 0.05f, 1e-3f);  // overlap + 2g
    pggtest::expectF32Near(bStart - aEnd, 0.05f, 1e-4f);                  // one-sided: exactly g
}

// --- 10. grind (middle-surface partition, §19 v1.10) ------------------------------
// sdf_grind(a, b, gap) = max(a, a - b + gap): a is cut along the middle
// surface {a == b} of the penetration, backed off by gap/2 per side, so the
// symmetric pair leaves a uniform slit of exactly `gap` no matter how deep
// the stones overlap — the "lap two stones together until they share a
// contact surface" joint. Contacts closer than gap are separated to gap;
// fields further than gap away leave `a` bit-exactly unchanged.

TEST(SdfGrind, SymmetricPairLeavesUniformGapCentredOnMiddle) {
    // Unit spheres 1.8 apart (overlap 0.2): middle surface at x = 0.9.
    pgg::RunResult r = pgg::run(
        "a = sdf_sphere(r = 1.0)\n"
        "anchor = transform(mesh_line(count = 1, length = 0.0), translate = (1.8, 0, 0))\n"
        "b = sdf_instance_on_points(anchor, source = sdf_sphere(r = 1.0))\n"
        "ga = sdf_grind(a, b, gap = 0.1)\n"
        "gb = sdf_grind(b, a, gap = 0.1)\n"
        "output ga\n"
        "output gb\n");
    pggtest::expectNoErrors(r);
    pgg::SdfPtr ga = sdfOutput(r, "ga");
    pgg::SdfPtr gb = sdfOutput(r, "gb");
    ASSERT_TRUE(ga);
    ASSERT_TRUE(gb);
    const float aEnd = crossOnRay(ga, glm::vec3(1, 0, 0), 0.5f, 1.0f);
    const float bStart = crossOnRay(gb, glm::vec3(1, 0, 0), 1.3f, 0.9f);
    pggtest::expectF32Near(aEnd, 0.85f, 1e-4f);   // midplane minus gap/2
    pggtest::expectF32Near(bStart, 0.95f, 1e-4f); // midplane plus gap/2
    pggtest::expectF32Near(bStart - aEnd, 0.1f, 1e-4f);  // slit == gap exactly
}

TEST(SdfGrind, ZeroGapSharesTheExactMiddleSurface) {
    pgg::RunResult r = pgg::run(
        "a = sdf_sphere(r = 1.0)\n"
        "anchor = transform(mesh_line(count = 1, length = 0.0), translate = (1.8, 0, 0))\n"
        "b = sdf_instance_on_points(anchor, source = sdf_sphere(r = 1.0))\n"
        "ga = sdf_grind(a, b)\n"
        "gb = sdf_grind(b, a)\n"
        "output ga\n"
        "output gb\n");
    pggtest::expectNoErrors(r);
    pgg::SdfPtr ga = sdfOutput(r, "ga");
    pgg::SdfPtr gb = sdfOutput(r, "gb");
    ASSERT_TRUE(ga);
    ASSERT_TRUE(gb);
    const float aEnd = crossOnRay(ga, glm::vec3(1, 0, 0), 0.5f, 1.0f);
    const float bStart = crossOnRay(gb, glm::vec3(1, 0, 0), 1.3f, 0.85f);
    pggtest::expectF32Near(aEnd, 0.9f, 1e-5f);
    pggtest::expectF32Near(bStart, 0.9f, 1e-5f);  // the same plane, lapped joint
}

TEST(SdfGrind, NoContactIsBitExactAndNearTouchSeparatesToGap) {
    pgg::RunResult r = pgg::run(
        "a = sdf_sphere(r = 1.0)\n"
        "far_anchor = transform(mesh_line(count = 1, length = 0.0), translate = (2.5, 0, 0))\n"
        "near_anchor = transform(mesh_line(count = 1, length = 0.0), translate = (2.02, 0, 0))\n"
        "b_far = sdf_instance_on_points(far_anchor, source = sdf_sphere(r = 1.0))\n"
        "b_near = sdf_instance_on_points(near_anchor, source = sdf_sphere(r = 1.0))\n"
        "g_far = sdf_grind(a, b_far, gap = 0.1)\n"
        "g_near = sdf_grind(a, b_near, gap = 0.1)\n"
        "output g_far\n"
        "output g_near\n"
        "output a\n");
    pggtest::expectNoErrors(r);
    pgg::SdfPtr gFar = sdfOutput(r, "g_far");
    pgg::SdfPtr gNear = sdfOutput(r, "g_near");
    pgg::SdfPtr a = sdfOutput(r, "a");
    ASSERT_TRUE(gFar);
    ASSERT_TRUE(gNear);
    ASSERT_TRUE(a);
    // 0.5 clearance > gap: the field is untouched, bit for bit.
    for (const glm::vec3 p :
         {glm::vec3(0, 0, 0), glm::vec3(0.9f, 0.2f, 0), glm::vec3(1, 0, 0), glm::vec3(-0.5f, 0.7f, 0.3f)})
        EXPECT_EQ(gFar->eval(p), a->eval(p)) << p.x << " " << p.y << " " << p.z;
    // 0.02 clearance < gap: A's face is pushed back to half the slit:
    // boundary at (2.02 - gap)/2 = 0.96.
    pggtest::expectF32Near(crossOnRay(gNear, glm::vec3(1, 0, 0), 0.5f, 1.05f), 0.96f, 1e-4f);
}

TEST(SdfGrind, UnionOfNeighboursGrindsEachContact) {
    // A between two neighbours: both flanks are cut to their own middle
    // surfaces (x = ±0.9 with gap 0.1 → ±0.85).
    pgg::RunResult r = pgg::run(
        "a = sdf_sphere(r = 1.0)\n"
        "r_anchor = transform(mesh_line(count = 1, length = 0.0), translate = (1.8, 0, 0))\n"
        "l_anchor = transform(mesh_line(count = 1, length = 0.0), translate = (-1.8, 0, 0))\n"
        "br = sdf_instance_on_points(r_anchor, source = sdf_sphere(r = 1.0))\n"
        "bl = sdf_instance_on_points(l_anchor, source = sdf_sphere(r = 1.0))\n"
        "g = sdf_grind(a, sdf_union(br, bl), gap = 0.1)\n"
        "output g\n");
    pggtest::expectNoErrors(r);
    pgg::SdfPtr g = sdfOutput(r, "g");
    ASSERT_TRUE(g);
    pggtest::expectF32Near(crossOnRay(g, glm::vec3(1, 0, 0), 0.5f, 1.0f), 0.85f, 1e-4f);
    pggtest::expectF32Near(crossOnRay(g, glm::vec3(-1, 0, 0), 0.5f, 1.0f), 0.85f, 1e-4f);
}

TEST(SdfGrind, MiddleSurfaceFollowsDisplacedNeighbour) {
    // Pseudo-SDF neighbour (fbm-roughened, as in boulder_field): the cut iso
    // of A is where b exceeds a by exactly gap — the middle surface tracks
    // field values, not geometric distances.
    const char* src =
        "root_rng = rng_from_seed(11)\n"
        "n_rng = split_rng(root_rng, key = \"n\")\n"
        "a = sdf_sphere(r = 1.0)\n"
        "anchor = transform(mesh_line(count = 1, length = 0.0), translate = (1.8, 0, 0))\n"
        "b = sdf_instance_on_points(anchor, source = sdf_displace(sdf_sphere(r = 1.0), amount = fbm(scale = 2.0, octaves = 3, rng = n_rng) * 0.04))\n"
        "g = sdf_grind(a, b, gap = 0.1)\n"
        "output g\n"
        "output a\n"
        "output b\n";
    pgg::RunResult r = pgg::run(src);
    pggtest::expectNoErrors(r);
    pgg::SdfPtr g = sdfOutput(r, "g");
    pgg::SdfPtr a = sdfOutput(r, "a");
    pgg::SdfPtr b = sdfOutput(r, "b");
    ASSERT_TRUE(g);
    ASSERT_TRUE(a);
    ASSERT_TRUE(b);
    for (const glm::vec3 dir : {glm::normalize(glm::vec3(1, 0.3f, 0.1f)), glm::normalize(glm::vec3(1, -0.2f, 0.35f)),
                                glm::normalize(glm::vec3(1, 0.1f, -0.3f))}) {
        SCOPED_TRACE("ray " + std::to_string(dir.x) + " " + std::to_string(dir.y) + " " + std::to_string(dir.z));
        const float t = crossOnRay(g, dir, 0.4f, 1.05f);
        pggtest::expectF32Near(b->eval(dir * t) - a->eval(dir * t), 0.1f, 1e-4f);
    }
}

}  // namespace
