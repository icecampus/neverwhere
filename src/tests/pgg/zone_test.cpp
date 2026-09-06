// E7 zone tests (spec §5.4): repeat/foreach execution semantics, the
// validator's state-port rules and the W004/W005 rng lint, static typecheck
// of zone bodies, laziness, nesting, thread-count invariance (N1) and the
// probe integration on zone targets.
#include <gtest/gtest.h>

#include <algorithm>

#include "pgg/eval.h"
#include "pgg/pgg.h"
#include "pgg/src/eval/fracture.h"
#include "test_utils.h"

namespace {

using pggtest::expectNoErrors;
using pggtest::geoContentHash;
using pggtest::geoOutput;
using pggtest::outputOf;

int countCode(const pgg::RunResult& r, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

int countDocCode(const pgg::Document& doc, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : doc.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

pgg::RunResult runSrc(const std::string& src, unsigned threads = 0) {
    pgg::RunParams p;
    p.threads = threads;
    return pgg::run(src, p);
}

// --- validator: state ports and the W004/W005 lint ----------------------------

TEST(ZoneValidate, ForeachItemRebindOnceIsLegalTwiceIsE102) {
    // The spec example rebinds the item (`piece = smooth(...)`): exactly one
    // rebind is the output-port binding, a second is E102.
    pgg::Document ok = pgg::parse(
        "b = box(size = (1, 1, 1))\n"
        "f = foreach piece in b {\n"
        "    piece = b\n"
        "}\n"
        "output f\n");
    EXPECT_EQ(countDocCode(ok, "E102"), 0);
    pgg::Document twice = pgg::parse(
        "b = box(size = (1, 1, 1))\n"
        "f = foreach piece in b {\n"
        "    piece = b\n"
        "    piece = b\n"
        "}\n"
        "output f\n");
    EXPECT_EQ(countDocCode(twice, "E102"), 1);
}

TEST(ZoneValidate, StatePortShadowingOuterNameIsE102) {
    // Two hits each: the port declaration shadows the outer name, and the
    // body binding of that name shadows it again.
    pgg::Document rep = pgg::parse(
        "cur = 1\n"
        "r = repeat (1, iterations = 2) |cur| {\n"
        "    cur = cur + 1\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countDocCode(rep, "E102"), 2);
    pgg::Document fe = pgg::parse(
        "x = box(size = (1, 1, 1))\n"
        "b = box(size = (2, 2, 2))\n"
        "f = foreach x in b {\n"
        "    x = b\n"
        "}\n"
        "output f\n");
    EXPECT_EQ(countDocCode(fe, "E102"), 2);
}

TEST(ZoneValidate, W004RepeatStochasticRngWithoutIteration) {
    pgg::Document doc = pgg::parse(
        "r_rng = rng_from_seed(1)\n"
        "pc = point_cloud(count = 2, bounds = (1, 1, 1), rng = r_rng)\n"
        "r = repeat (pc, iterations = 2) |state| {\n"
        "    state = set_position(state, offset = vnoise(scale = 2.0, rng = r_rng) * 0.1)\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countDocCode(doc, "W004"), 1);
    EXPECT_EQ(countDocCode(doc, "W005"), 0);
}

TEST(ZoneValidate, W004SilentWhenRngDependsOnIteration) {
    pgg::Document doc = pgg::parse(
        "r_rng = rng_from_seed(1)\n"
        "pc = point_cloud(count = 2, bounds = (1, 1, 1), rng = r_rng)\n"
        "r = repeat (pc, iterations = 2) |state| {\n"
        "    it_rng = split_rng(r_rng, key = @iteration)\n"
        "    state = set_position(state, offset = vnoise(scale = 2.0, rng = it_rng) * 0.1)\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countDocCode(doc, "W004"), 0);
}

TEST(ZoneValidate, W005ForeachStochasticRngWithoutPieceIndex) {
    pgg::Document doc = pgg::parse(
        "r_rng = rng_from_seed(1)\n"
        "b = box(size = (1, 1, 1))\n"
        "f = foreach piece in b {\n"
        "    piece = set_position(piece, offset = vnoise(scale = 2.0, rng = r_rng) * 0.1)\n"
        "}\n"
        "output f\n");
    EXPECT_EQ(countDocCode(doc, "W005"), 1);
    EXPECT_EQ(countDocCode(doc, "W004"), 0);
}

TEST(ZoneValidate, W005SilentThroughAliasChain) {
    // piece_rng -> jrng -> random_vec: the @piece_index dependency resolves
    // through the local SSA alias chain (split_rng calls included).
    pgg::Document doc = pgg::parse(
        "r_rng = rng_from_seed(1)\n"
        "b = box(size = (1, 1, 1))\n"
        "f = foreach piece in b {\n"
        "    piece_rng = split_rng(r_rng, key = @piece_index)\n"
        "    jrng = split_rng(piece_rng, key = \"j\")\n"
        "    pj = random_vec(lo = (0, 0, 0), hi = (1, 1, 1), rng = jrng, counter = 0)\n"
        "    piece = set_position(piece, offset = pj)\n"
        "}\n"
        "output f\n");
    EXPECT_EQ(countDocCode(doc, "W005"), 0);
}

TEST(ZoneValidate, NestedZonesApplyBothRules) {
    // A stochastic op inside a repeat inside a foreach must satisfy both
    // enclosing rules; depending on @iteration alone still warns W005.
    pgg::Document doc = pgg::parse(
        "r_rng = rng_from_seed(1)\n"
        "pc = point_cloud(count = 2, bounds = (1, 1, 1), rng = r_rng)\n"
        "b = box(size = (1, 1, 1))\n"
        "f = foreach piece in b {\n"
        "    inner = repeat (pc, iterations = 2) |state| {\n"
        "        it_rng = split_rng(r_rng, key = @iteration)\n"
        "        state = set_position(state, offset = vnoise(scale = 2.0, rng = it_rng) * 0.1)\n"
        "    }\n"
        "    piece = piece\n"
        "}\n"
        "output f\n");
    EXPECT_EQ(countDocCode(doc, "W004"), 0);
    EXPECT_EQ(countDocCode(doc, "W005"), 1);
}

// --- repeat semantics ----------------------------------------------------------

TEST(ZoneRepeat, ScalarFoldSumsIterations) {
    pgg::RunResult r = runSrc(
        "r = repeat (1, iterations = 5) |cur| {\n"
        "    cur = cur + 1\n"
        "}\n"
        "output r\n");
    expectNoErrors(r);
    ASSERT_EQ(r.outputs.size(), 1u);
    EXPECT_EQ(pgg::asInt(r.outputs[0].value), 6);
}

TEST(ZoneRepeat, ZeroIterationsIsIdentity) {
    pgg::RunResult r = runSrc(
        "r = repeat (99, iterations = 0) |cur| {\n"
        "    cur = 0\n"
        "}\n"
        "output r\n");
    expectNoErrors(r);
    EXPECT_EQ(pgg::asInt(r.outputs[0].value), 99);
}

TEST(ZoneRepeat, NegativeIterationsIsE607) {
    pgg::RunResult r = runSrc(
        "r = repeat (7, iterations = 0 - 2) |cur| {\n"
        "    cur = cur + 1\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countCode(r, "E607"), 1);
    // Clamped to 0: the output is the input.
    EXPECT_EQ(pgg::asInt(r.outputs[0].value), 7);
}

TEST(ZoneRepeat, MultiStatePortsFold) {
    pgg::RunResult r = runSrc(
        "g = box(size = (1, 1, 1))\n"
        "lo, hi = repeat (bbox(g), iterations = 2) |a, b| {\n"
        "    a = a + (1, 0, 0)\n"
        "    b = b + (1, 0, 0)\n"
        "}\n"
        "output lo\n"
        "output hi\n");
    expectNoErrors(r);
    const glm::vec3 lo = pgg::asVec3(*outputOf(r, "lo"));
    const glm::vec3 hi = pgg::asVec3(*outputOf(r, "hi"));
    pggtest::expectVec3Near(lo, glm::vec3(1.5f, -0.5f, -0.5f));
    pggtest::expectVec3Near(hi, glm::vec3(2.5f, 0.5f, 0.5f));
}

TEST(ZoneRepeat, GeoStateWithPerIterationRngIsDeterministic) {
    const std::string src =
        "root_rng = rng_from_seed(42)\n"
        "r_rng = split_rng(root_rng, key = \"r\")\n"
        "pc_rng = split_rng(root_rng, key = \"pc\")\n"
        "pts = point_cloud(count = 8, bounds = (1, 1, 1), rng = pc_rng)\n"
        "r = repeat (pts, iterations = 3) |state| {\n"
        "    it_rng = split_rng(r_rng, key = @iteration)\n"
        "    state = set_position(state, offset = vnoise(scale = 2.0, rng = it_rng) * 0.1)\n"
        "}\n"
        "output r\n";
    pgg::RunResult a = runSrc(src);
    pgg::RunResult b = runSrc(src);
    expectNoErrors(a);
    expectNoErrors(b);
    EXPECT_EQ(*pgg::asGeo(a.outputs[0].value)->positions, *pgg::asGeo(b.outputs[0].value)->positions);
    // The three displaced iterations really moved the points.
    pgg::RunResult in = runSrc(
        "pc_rng = split_rng(rng_from_seed(42), key = \"pc\")\n"
        "pts = point_cloud(count = 8, bounds = (1, 1, 1), rng = pc_rng)\n"
        "output pts\n");
    EXPECT_NE(*pgg::asGeo(a.outputs[0].value)->positions, *pgg::asGeo(in.outputs[0].value)->positions);
}

TEST(ZoneRepeat, NestedRepeatReadsInnerIteration) {
    // @iteration resolves to the innermost enclosing repeat.
    pgg::RunResult r = runSrc(
        "r = repeat (0, iterations = 2) |i| {\n"
        "    inner = repeat (i, iterations = 3) |j| {\n"
        "        j = j + @iteration\n"
        "    }\n"
        "    i = i + inner\n"
        "}\n"
        "output r\n");
    expectNoErrors(r);
    // outer 0: inner = 0+0+1+2 = 3, i = 3; outer 1: inner = 3+3 = 6, i = 9.
    EXPECT_EQ(pgg::asInt(r.outputs[0].value), 9);
}

TEST(ZoneRepeat, LazinessUnpulledZoneNeverRuns) {
    pgg::RunResult r = runSrc(
        "a = ico_sphere(subdiv = 1, radius = 1.0)\n"
        "z = repeat (a, iterations = 3) |cur| {\n"
        "    cur = set_position(cur, offset = @N * 0.0)\n"
        "}\n"
        "output a\n");
    expectNoErrors(r);
    ASSERT_EQ(r.outputs.size(), 1u);
    EXPECT_EQ(r.outputs[0].name, "a");
    EXPECT_EQ(r.stats.fieldsEvaluated, 0u);
}

TEST(ZoneRepeat, StatePortUnboundIsE204) {
    pgg::RunResult r = runSrc(
        "r = repeat (1, iterations = 2) |cur| {\n"
        "    x = cur + 1\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countCode(r, "E204"), 1);
}

TEST(ZoneRepeat, StatePortTypeDriftIsE204) {
    pgg::RunResult r = runSrc(
        "r = repeat (1, iterations = 2) |cur| {\n"
        "    cur = (1, 2, 3)\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countCode(r, "E204"), 1);
}

TEST(ZoneRepeat, TargetArityMismatchIsE202) {
    pgg::RunResult r = runSrc(
        "a, b = repeat (1, iterations = 2) |cur| {\n"
        "    cur = cur + 1\n"
        "}\n"
        "output a\n");
    EXPECT_EQ(countCode(r, "E202"), 1);
}

TEST(ZoneRepeat, IterationsFieldIsE205) {
    pgg::RunResult r = runSrc(
        "g = box(size = (1, 1, 1))\n"
        "r = repeat (g, iterations = @index) |cur| {\n"
        "    cur = cur\n"
        "}\n"
        "output r\n");
    EXPECT_EQ(countCode(r, "E205"), 1);
}

TEST(ZoneRepeat, IterationOutsideZoneIsE302) {
    pgg::RunResult r = runSrc(
        "x = @iteration + 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E302"), 1);
}

// --- foreach semantics ---------------------------------------------------------

const char* kTwoBoxes =
    "a = box(size = (1, 1, 1))\n"
    "b = transform(box(size = (1, 1, 1)), translate = (5, 0, 0))\n"
    "m = merge(a, b)\n";

TEST(ZoneForeach, SplitsProcessesMerges) {
    pgg::RunResult r = runSrc(std::string(kTwoBoxes) +
        "chunks = foreach piece in m {\n"
        "    piece = smooth(piece, iterations = 1)\n"
        "}\n"
        "output chunks\n");
    expectNoErrors(r);
    pgg::GeoPtr g = pgg::asGeo(r.outputs[0].value);
    ASSERT_EQ(g->kind, pgg::GeoKind::Mesh);
    EXPECT_EQ(g->faceCount(), 12u);
    // The rigid merge preserves the piece split: two islands again.
    size_t islands = 0;
    pgg::computeIslands(*g, islands);
    EXPECT_EQ(islands, 2u);
    // The smoothing shrunk every box toward its centroid.
    glm::vec3 mn, mx;
    pgg::geoBBox(*g, mn, mx);
    EXPECT_GT(mn.x, -0.5f);
    EXPECT_LT(mx.x, 5.5f);
}

TEST(ZoneForeach, PerPieceRngIsDeterministicAndThreadInvariant) {
    const std::string src = std::string(kTwoBoxes) +
        "parts_rng = split_rng(rng_from_seed(7), key = \"parts\")\n"
        "chunks = foreach piece in m {\n"
        "    piece_rng = split_rng(parts_rng, key = @piece_index)\n"
        "    jrng = split_rng(piece_rng, key = \"j\")\n"
        "    pj = random_vec(lo = (0, 0, 0), hi = (1, 1, 1), rng = jrng, counter = 0)\n"
        "    piece = set_position(piece, offset = pj * 0.2)\n"
        "}\n"
        "output chunks\n";
    pgg::RunResult t1 = runSrc(src, 1);
    pgg::RunResult t4 = runSrc(src, 4);
    pgg::RunResult again = runSrc(src, 4);
    expectNoErrors(t1);
    expectNoErrors(t4);
    expectNoErrors(again);
    EXPECT_EQ(geoContentHash(pgg::asGeo(t1.outputs[0].value)), geoContentHash(pgg::asGeo(t4.outputs[0].value)));
    EXPECT_EQ(geoContentHash(pgg::asGeo(t4.outputs[0].value)),
              geoContentHash(pgg::asGeo(again.outputs[0].value)));
}

TEST(ZoneForeach, EmptyCollectionYieldsEmptyMesh) {
    pgg::RunResult r = runSrc(
        "empty = mesh_from_sdf(sdf_sphere(r = 1.0), voxel = 0.5, iso = 100.0)\n"
        "f = foreach piece in empty {\n"
        "    piece = smooth(piece, iterations = 1)\n"
        "}\n"
        "output f\n");
    expectNoErrors(r);
    pgg::GeoPtr g = pgg::asGeo(r.outputs[0].value);
    ASSERT_TRUE(g);
    EXPECT_EQ(g->faceCount(), 0u);
    EXPECT_EQ(g->pointCount(), 0u);
}

TEST(ZoneForeach, TaggedIslandsDriveTheSplit) {
    pgg::RunResult r = runSrc(std::string(kTwoBoxes) +
        "tagged = islands(m)\n"
        "chunks = foreach piece in tagged {\n"
        "    piece = set_position(piece, offset = (0, 0, 0))\n"
        "}\n"
        "output chunks\n");
    expectNoErrors(r);
    pgg::GeoPtr g = pgg::asGeo(r.outputs[0].value);
    ASSERT_TRUE(g);
    EXPECT_EQ(g->faceCount(), 12u);
    size_t islands = 0;
    pgg::computeIslands(*g, islands);
    EXPECT_EQ(islands, 2u);
}

TEST(ZoneForeach, ItemUnboundIsE204) {
    pgg::RunResult r = runSrc(
        "b = box(size = (1, 1, 1))\n"
        "f = foreach piece in b {\n"
        "    q = piece\n"
        "}\n"
        "output f\n");
    EXPECT_EQ(countCode(r, "E204"), 1);
}

// --- foreach over points: the row model (v1.20) -------------------------------

TEST(ZoneForeach, OverPointsIteratesOnePiecePerPointInIndexOrder) {
    // Each row builds a line whose length is a per-row attribute; the merged
    // output keeps piece order (row 0 first) and takes the body's kind.
    pgg::RunResult r = runSrc(
        "rows0 = mesh_line(count = 3, length = 2.0, dir = (0, 1, 0))\n"
        "rows = set(rows0, \"n\", 2 + @index, domain = points)\n"
        "pts = foreach row in rows {\n"
        "    n = value(@n, on = row)\n"
        "    line = mesh_line(count = n, length = f32(n - 1), dir = (1, 0, 0))\n"
        "    row = transform(line, translate = value(@P, on = row))\n"
        "}\n"
        "output pts\n");
    expectNoErrors(r);
    ASSERT_EQ(r.outputs.size(), 1u);
    pgg::GeoPtr g = pgg::asGeo(r.outputs[0].value);
    EXPECT_EQ(g->kind, pgg::GeoKind::Points);
    ASSERT_EQ(g->pointCount(), 2u + 3u + 4u);
    EXPECT_EQ((*g->positions)[0], glm::vec3(0, 0, 0));
    EXPECT_EQ((*g->positions)[1], glm::vec3(1, 0, 0));
    EXPECT_EQ((*g->positions)[2], glm::vec3(0, 1, 0));
    EXPECT_EQ((*g->positions)[8], glm::vec3(3, 2, 0));
}

TEST(ZoneForeach, PointRowMayBecomeAMeshAndPieceIndexIsTheRowNumber) {
    // A row of points becomes a mesh (kind follows the body); @piece_index is
    // the row number while @index inside a one-point piece is always 0.
    pgg::RunResult r = runSrc(
        "rows = mesh_line(count = 3, length = 2.0, dir = (0, 1, 0))\n"
        "m = foreach row in rows {\n"
        "    b = box(size = vec3(1.0 + f32(@piece_index), 0.5, 0.5))\n"
        "    row = transform(b, translate = value(@P, on = row))\n"
        "}\n"
        "output m\n");
    expectNoErrors(r);
    pgg::GeoPtr g = pgg::asGeo(r.outputs[0].value);
    EXPECT_EQ(g->kind, pgg::GeoKind::Mesh);
    EXPECT_EQ(g->faceCount(), 18u);
    EXPECT_EQ(g->pointCount(), 24u);
    // Third row: box of width 3 centred at y = 2 -> x spans [-1.5, 1.5].
    float maxX = -1e9f;
    for (const glm::vec3& p : *g->positions) maxX = std::max(maxX, p.x);
    EXPECT_NEAR(maxX, 1.5f, 1e-5f);
}

TEST(ZoneForeach, OverInstancesIsE204AndMixedKindsAreE607) {
    pgg::RunResult a = runSrc(
        "pc = point_cloud(count = 3, bounds = (1, 1, 1), rng = rng_from_seed(1))\n"
        "inst = instance_on_points(pc, box(size = (1, 1, 1)))\n"
        "f = foreach piece in inst {\n"
        "    piece = piece\n"
        "}\n"
        "output f\n");
    EXPECT_EQ(countCode(a, "E204"), 1);
    // The item port must end as mesh/points: a non-geo rebind is E204.
    pgg::RunResult b = runSrc(
        "pc = point_cloud(count = 3, bounds = (1, 1, 1), rng = rng_from_seed(1))\n"
        "f = foreach piece in pc {\n"
        "    piece = 1.0\n"
        "}\n"
        "output f\n");
    EXPECT_EQ(countCode(b, "E204"), 1);
}

TEST(ZoneForeach, ValueAggregatorNeedsExactlyOnePoint) {
    pgg::RunResult r = runSrc(
        "pts = mesh_line(count = 3, length = 2.0, dir = (1, 0, 0))\n"
        "a = value(@P, on = pts, where = @index == 1)\n"
        "b = value(@P, on = pts)\n"
        "c = value(@P, on = pts, where = @index > 5)\n"
        "output a\n"
        "output b\n"
        "output c\n");
    EXPECT_EQ(countCode(r, "E601"), 2);
    ASSERT_GE(r.outputs.size(), 1u);
    EXPECT_EQ(pgg::valueBase(r.outputs[0].value), pgg::ScalarType::Vec3);
    EXPECT_EQ(pgg::asVec3(r.outputs[0].value), glm::vec3(1, 0, 0));
}

TEST(ZoneForeach, PieceIndexOutsideZoneIsE302) {
    pgg::RunResult r = runSrc(
        "x = @piece_index + 1\n"
        "output x\n");
    EXPECT_EQ(countCode(r, "E302"), 1);
}

TEST(ZoneForeach, ProbeOnZoneTarget) {
    pgg::RunParams p;
    p.probes = {"chunks:schema"};
    pgg::RunResult r = pgg::run(std::string(kTwoBoxes) +
                                    "chunks = foreach piece in m {\n"
                                    "    piece = smooth(piece, iterations = 1)\n"
                                    "}\n"
                                    "output chunks\n",
                                p);
    expectNoErrors(r);
    // Probe-only run: the declared output is not computed.
    EXPECT_TRUE(r.outputs.empty());
    ASSERT_EQ(r.probes.size(), 1u);
    EXPECT_EQ(r.probes[0].path, "chunks");
    EXPECT_EQ(r.probes[0].inspector, "schema");
    EXPECT_NE(r.probes[0].text.find("mesh"), std::string::npos);
}

// --- defs x zones (v1.20: def calls in bodies, zones in def bodies) -----------

TEST(ZoneExpand, DefCallInZoneBodyIsInlinedPerIteration) {
    pgg::RunResult r = runSrc(
        "def mk(s: f32) -> (out: geo<mesh>) {\n"
        "    \"\"\"make a box\"\"\"\n"
        "    out = box(size = vec3(s, s, s))\n"
        "}\n"
        "r = repeat (mk(1.0), iterations = 2) |cur| {\n"
        "    cur = merge(cur, mk(0.5 + f32(@iteration)))\n"
        "}\n"
        "output r\n");
    expectNoErrors(r);
    pgg::GeoPtr g = pgg::asGeo(r.outputs[0].value);
    EXPECT_EQ(g->faceCount(), 18u);  // seed box + one box per iteration
}

TEST(ZoneExpand, ZoneInDefBodyIsClonedPerInstance) {
    // Two instances of a def with a repeat zone inside: each gets its own
    // zone (renamed under the instance), results differ by the argument.
    pgg::RunResult r = runSrc(
        "def stack(g: geo<mesh>, n: int) -> (out: geo<mesh>) {\n"
        "    \"\"\"n copies of g, shifted up\"\"\"\n"
        "    r = repeat (g, iterations = n) |cur| {\n"
        "        cur = merge(cur, transform(g, translate = vec3(0, 1.0 + f32(@iteration), 0)))\n"
        "    }\n"
        "    out = r\n"
        "}\n"
        "b = box(size = (1, 1, 1))\n"
        "x = stack(b, n = 2)\n"
        "y = stack(b, n = 4)\n"
        "output x\n"
        "output y\n");
    expectNoErrors(r);
    ASSERT_EQ(r.outputs.size(), 2u);
    EXPECT_EQ(pgg::asGeo(r.outputs[0].value)->faceCount(), 18u);
    EXPECT_EQ(pgg::asGeo(r.outputs[1].value)->faceCount(), 30u);
}

TEST(ZoneExpand, DefWithForeachInsideDefCalledInsideForeach) {
    // Row model end to end: the outer foreach walks the rows, the def owns a
    // foreach over the row's anchor points and calls another def per point.
    pgg::RunResult r = runSrc(
        "def brick(at: vec3, w: f32) -> (b: geo<mesh>) {\n"
        "    \"\"\"one brick\"\"\"\n"
        "    expect w > 0.0\n"
        "    b = transform(box(size = vec3(w, 0.2, 0.2)), translate = at)\n"
        "}\n"
        "def course(n: int, y: f32) -> (row: geo<mesh>) {\n"
        "    \"\"\"a row of n bricks\"\"\"\n"
        "    pts = mesh_line(count = n, length = f32(n - 1) * 0.5, dir = (1, 0, 0))\n"
        "    row = foreach p in pts {\n"
        "        p = brick(at = centroid(p) + vec3(0, y, 0), w = 0.45)\n"
        "    }\n"
        "}\n"
        "rows = mesh_line(count = 3, length = 2.0, dir = (0, 1, 0))\n"
        "wall = foreach r in rows {\n"
        "    r = course(n = 2 + @piece_index, y = f32(@piece_index))\n"
        "}\n"
        "output wall\n");
    expectNoErrors(r);
    EXPECT_EQ(pgg::asGeo(r.outputs[0].value)->faceCount(), (2u + 3u + 4u) * 6u);
    // Same result at any lane count (N1).
    pgg::RunResult r1 = runSrc(
        "def brick(at: vec3, w: f32) -> (b: geo<mesh>) {\n"
        "    \"\"\"one brick\"\"\"\n"
        "    expect w > 0.0\n"
        "    b = transform(box(size = vec3(w, 0.2, 0.2)), translate = at)\n"
        "}\n"
        "def course(n: int, y: f32) -> (row: geo<mesh>) {\n"
        "    \"\"\"a row of n bricks\"\"\"\n"
        "    pts = mesh_line(count = n, length = f32(n - 1) * 0.5, dir = (1, 0, 0))\n"
        "    row = foreach p in pts {\n"
        "        p = brick(at = centroid(p) + vec3(0, y, 0), w = 0.45)\n"
        "    }\n"
        "}\n"
        "rows = mesh_line(count = 3, length = 2.0, dir = (0, 1, 0))\n"
        "wall = foreach r in rows {\n"
        "    r = course(n = 2 + @piece_index, y = f32(@piece_index))\n"
        "}\n"
        "output wall\n",
        1);
    EXPECT_EQ(geoContentHash(pgg::asGeo(r.outputs[0].value)), geoContentHash(pgg::asGeo(r1.outputs[0].value)));
}

TEST(ZoneExpand, ContractsOfInstancesInsideZoneBodiesFire) {
    pgg::RunResult r = runSrc(
        "def brick(w: f32) -> (b: geo<mesh>) {\n"
        "    \"\"\"one brick\"\"\"\n"
        "    expect w > 0.0\n"
        "    b = box(size = vec3(w, 0.2, 0.2))\n"
        "}\n"
        "pts = mesh_line(count = 3, length = 2.0, dir = (1, 0, 0))\n"
        "m = foreach p in pts {\n"
        "    p = brick(w = 1.0 - f32(@piece_index))\n"  // piece 1 -> w == 0 -> E303
        "}\n"
        "output m\n");
    EXPECT_GE(countCode(r, "E303"), 1);
}

}  // namespace
