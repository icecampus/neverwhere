// E7 fracture/islands tests (spec §8.3, §8.11): the pure geometry of
// computeIslands/splitMeshPieces/mergeMeshPieces (piece order contract, union
// column semantics), the islands builtin, the SDF-Voronoi fracture model
// (site dedupe, empty-piece skipping, E608 paths) and the two corpus etalons
// (§15 E7: repeat settle + fracture pipeline) with determinism/N1 goldens.
#include <gtest/gtest.h>

#include <algorithm>
#include <unordered_set>

#include "pgg/eval.h"
#include "pgg/src/eval/builtins.h"
#include "pgg/src/eval/fracture.h"
#include "test_utils.h"

namespace {

using pggtest::expectNoErrors;
using pggtest::expectVec3Near;
using pggtest::geoContentHash;
using pggtest::outputOf;

int countCode(const pgg::RunResult& r, const std::string& code) {
    int n = 0;
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (d.code == code) n += 1;
    return n;
}

pgg::RunResult runSrc(const std::string& src, unsigned threads = 0) {
    pgg::RunParams p;
    p.threads = threads;
    return pgg::run(src, p);
}

pgg::GeoPtr twoQuads() {
    std::vector<glm::vec3> pos = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0},
                                  {5, 0, 0}, {6, 0, 0}, {6, 1, 0}, {5, 1, 0}};
    std::vector<int32_t> corners = {0, 1, 2, 3, 4, 5, 6, 7};
    std::vector<int32_t> offsets = {0, 4, 8};
    return pgg::makeMesh(std::move(pos), std::move(corners), std::move(offsets));
}

std::vector<int64_t> islandIds(const pgg::Geo& g) {
    const pgg::AttrSet* fa = g.attrs(pgg::Domain::Faces);
    const pgg::AttrColumn* col = fa ? fa->find("island_id") : nullptr;
    if (!col) return {};
    return *std::get<std::shared_ptr<const std::vector<int64_t>>>(col->data);
}

// --- pure geometry: islands/split/merge ----------------------------------------

TEST(FractureGeometry, UnionFindIslandsInMinFaceOrder) {
    size_t count = 0;
    const std::vector<int32_t> ids = pgg::computeIslands(*twoQuads(), count);
    EXPECT_EQ(count, 2u);
    ASSERT_EQ(ids.size(), 2u);
    EXPECT_EQ(ids[0], 0);
    EXPECT_EQ(ids[1], 1);  // ascending minimum face index
}

TEST(FractureGeometry, TaggedIdsGroupByMinFaceNotByTagValue) {
    pgg::GeoPtr g = twoQuads();
    // Tags {5, 3}: the dense id follows the component's minimum face index,
    // not the tag value (tag 3 sorts first but its component starts later).
    pgg::AttrSet attrs;
    attrs.columns["island_id"] =
        pgg::AttrColumn{pgg::ColumnData(std::make_shared<const std::vector<int64_t>>(std::vector<int64_t>{5, 3}))};
    g = pgg::withAttrs(*g, pgg::Domain::Faces, std::make_shared<const pgg::AttrSet>(std::move(attrs)));
    size_t count = 0;
    const std::vector<int32_t> ids = pgg::computeIslands(*g, count);
    EXPECT_EQ(count, 2u);
    EXPECT_EQ(ids[0], 0);  // face 0: tag 5 -> first component by min face
    EXPECT_EQ(ids[1], 1);  // face 1: tag 3 -> second component
}

TEST(FractureGeometry, SplitMergeRoundTripPreservesShape) {
    pgg::RunResult r = runSrc(
        "a = box(size = (1, 1, 1))\n"
        "b = transform(box(size = (1, 1, 1)), translate = (5, 0, 0))\n"
        "m = merge(a, b)\n"
        "output m\n");
    expectNoErrors(r);
    const pgg::Geo& src = *pgg::asGeo(r.outputs[0].value);
    const std::vector<pgg::GeoPtr> pieces = pgg::splitMeshPieces(src);
    ASSERT_EQ(pieces.size(), 2u);
    size_t faceSum = 0;
    for (const pgg::GeoPtr& p : pieces) {
        EXPECT_EQ(pgg::nonManifoldEdgeCount(*p), 0u);  // every piece stays watertight
        faceSum += p->faceCount();
    }
    EXPECT_EQ(faceSum, src.faceCount());
    pgg::GeoPtr merged = pgg::mergeMeshPieces(pieces);
    EXPECT_EQ(merged->pointCount(), src.pointCount());
    EXPECT_EQ(merged->cornerCount(), src.cornerCount());
    EXPECT_EQ(merged->faceCount(), src.faceCount());
    glm::vec3 amn, amx, bmn, bmx;
    pgg::geoBBox(src, amn, amx);
    pgg::geoBBox(*merged, bmn, bmx);
    EXPECT_EQ(amn, bmn);
    EXPECT_EQ(amx, bmx);
    // NOTE: the split reindexes points per piece (corner-first order), so the
    // round trip preserves the geometry but not the vertex order.
}

TEST(FractureGeometry, MergeUnionSemanticsZeroFillsMissingAttrs) {
    pgg::GeoPtr a = twoQuads();
    pgg::AttrSet attrs;
    attrs.columns["weight"] = pgg::AttrColumn{pgg::ColumnData(
        std::make_shared<const std::vector<float>>(std::vector<float>(a->pointCount(), 1.0f)))};
    a = pgg::withAttrs(*a, pgg::Domain::Points, std::make_shared<const pgg::AttrSet>(std::move(attrs)));
    std::vector<pgg::GeoPtr> pieces = pgg::splitMeshPieces(*a);
    ASSERT_EQ(pieces.size(), 2u);
    // Only the first piece carries the attribute: the merge zero-fills the rest.
    pgg::AttrSet none;
    pieces[1] = pgg::withAttrs(*pieces[1], pgg::Domain::Points,
                               std::make_shared<const pgg::AttrSet>(std::move(none)));
    pgg::GeoPtr merged = pgg::mergeMeshPieces(pieces);
    const pgg::AttrSet* pa = merged->attrs(pgg::Domain::Points);
    ASSERT_TRUE(pa);
    const pgg::AttrColumn* col = pa->find("weight");
    ASSERT_TRUE(col);
    const auto& w = *std::get<std::shared_ptr<const std::vector<float>>>(col->data);
    ASSERT_EQ(w.size(), merged->pointCount());
    for (size_t i = 0; i < pieces[0]->pointCount(); ++i) EXPECT_EQ(w[i], 1.0f);
    for (size_t i = pieces[0]->pointCount(); i < w.size(); ++i) EXPECT_EQ(w[i], 0.0f);
}

// --- islands builtin -------------------------------------------------------------

TEST(Islands, WritesDenseIdsByMinFace) {
    pgg::RunResult r = runSrc(
        "a = box(size = (1, 1, 1))\n"
        "b = transform(box(size = (1, 1, 1)), translate = (5, 0, 0))\n"
        "m = merge(a, b)\n"
        "tagged = islands(m)\n"
        "output tagged\n");
    expectNoErrors(r);
    const std::vector<int64_t> ids = islandIds(*pgg::asGeo(r.outputs[0].value));
    ASSERT_EQ(ids.size(), 12u);
    for (size_t i = 0; i < 6; ++i) EXPECT_EQ(ids[i], 0);
    for (size_t i = 6; i < 12; ++i) EXPECT_EQ(ids[i], 1);
}

TEST(Islands, ConnectedMeshGetsSingleIsland) {
    pgg::RunResult r = runSrc(
        "b = box(size = (1, 1, 1))\n"
        "tagged = islands(b)\n"
        "output tagged\n");
    expectNoErrors(r);
    const std::vector<int64_t> ids = islandIds(*pgg::asGeo(r.outputs[0].value));
    ASSERT_EQ(ids.size(), 6u);
    for (int64_t id : ids) EXPECT_EQ(id, 0);
}

// --- fracture builtin ------------------------------------------------------------

const char* kRock =
    "rock = ico_sphere(subdiv = 2, radius = 1.0)\n";

TEST(Fracture, ZeroSitesIsE608) {
    pgg::RunResult r = runSrc(std::string(kRock) +
        "sites = point_cloud(count = 0, bounds = (1, 1, 1), rng = rng_from_seed(1))\n"
        "f = fracture(rock, planes = sites, rng = rng_from_seed(2))\n"
        "output f\n");
    EXPECT_EQ(countCode(r, "E608"), 1);
}

TEST(Fracture, EmptyMeshIsE608) {
    pgg::RunResult r = runSrc(
        "empty = mesh_from_sdf(sdf_sphere(r = 1.0), voxel = 0.5, iso = 100.0)\n"
        "sites = point_cloud(count = 2, bounds = (1, 1, 1), rng = rng_from_seed(1))\n"
        "f = fracture(empty, planes = sites, rng = rng_from_seed(2))\n"
        "output f\n");
    EXPECT_EQ(countCode(r, "E608"), 1);
}

TEST(Fracture, SingleSiteTagsWithoutCut) {
    pgg::RunResult r = runSrc(
        "b = box(size = (1, 1, 1))\n"
        "sites = point_cloud(count = 1, bounds = (1, 1, 1), rng = rng_from_seed(1))\n"
        "f = fracture(b, planes = sites, rng = rng_from_seed(2))\n"
        "output f\n");
    expectNoErrors(r);
    const pgg::Geo& g = *pgg::asGeo(r.outputs[0].value);
    EXPECT_EQ(g.faceCount(), 6u);
    EXPECT_EQ(g.pointCount(), 8u);
    const std::vector<int64_t> ids = islandIds(g);
    ASSERT_EQ(ids.size(), 6u);
    for (int64_t id : ids) EXPECT_EQ(id, 0);
}

TEST(Fracture, DuplicateSitesDedupKeepFirst) {
    // Two identical sites dedupe to one cell: the mesh passes through tagged 0.
    pgg::RunResult r = runSrc(
        "b = box(size = (1, 1, 1))\n"
        "one = point_cloud(count = 1, bounds = (1, 1, 1), rng = rng_from_seed(1))\n"
        "sites = merge(one, one)\n"
        "f = fracture(b, planes = sites, rng = rng_from_seed(2))\n"
        "output f\n");
    expectNoErrors(r);
    const pgg::Geo& g = *pgg::asGeo(r.outputs[0].value);
    EXPECT_EQ(g.faceCount(), 6u);
    const std::vector<int64_t> ids = islandIds(g);
    for (int64_t id : ids) EXPECT_EQ(id, 0);
}

TEST(Fracture, CutsIntoWatertightPieces) {
    pgg::RunResult r = runSrc(std::string(kRock) +
        "sites = point_cloud(count = 3, bounds = (1.2, 1.2, 1.2), rng = rng_from_seed(42))\n"
        "f = fracture(rock, planes = sites, rng = rng_from_seed(3))\n"
        "output f\n",
                              4);
    expectNoErrors(r);
    const pgg::Geo& g = *pgg::asGeo(r.outputs[0].value);
    const std::vector<int64_t> ids = islandIds(g);
    ASSERT_EQ(ids.size(), g.faceCount());
    const std::unordered_set<int64_t> unique(ids.begin(), ids.end());
    // All three sites of this seed cut non-empty pieces; ids are dense 0..2.
    EXPECT_EQ(unique.size(), 3u);
    EXPECT_EQ(*std::max_element(unique.begin(), unique.end()), 2);
    const std::vector<pgg::GeoPtr> pieces = pgg::splitMeshPieces(g);
    ASSERT_EQ(pieces.size(), unique.size());
    size_t faceSum = 0;
    for (const pgg::GeoPtr& p : pieces) {
        EXPECT_EQ(pgg::nonManifoldEdgeCount(*p), 0u);  // cut caps are watertight
        faceSum += p->faceCount();
    }
    EXPECT_EQ(faceSum, g.faceCount());
}

// --- corpus etalons (spec §15 E7) -----------------------------------------------

const std::string kSettleCorpus = std::string(PGG_CORPUS_DIR) + "/e7_repeat_settle.pgg";
const std::string kFractureCorpus = std::string(PGG_CORPUS_DIR) + "/e7_fracture.pgg";

TEST(EvalE7, RepeatSettleCorpusMatchesGoldens) {
    pgg::RunParams p;
    p.threads = 4;
    pgg::RunResult r = pgg::runFile(kSettleCorpus, p);
    expectNoErrors(r);
    ASSERT_EQ(r.outputs.size(), 4u);
    // The scatter relaxes onto the terrain: the mean distance drops ~7x.
    const float before = pgg::asF32(*outputOf(r, "dist_before"));
    const float after = pgg::asF32(*outputOf(r, "dist_after"));
    pggtest::expectF32Near(before, 1.42085f, 1e-4f);
    pggtest::expectF32Near(after, 0.211595f, 1e-4f);
    EXPECT_LT(after, before * 0.5f);
    const pgg::Geo& settled = *pgg::asGeo(*outputOf(r, "settled"));
    ASSERT_EQ(settled.kind, pgg::GeoKind::Points);
    EXPECT_EQ(settled.pointCount(), 12u);
    glm::vec3 mn, mx;
    pgg::geoBBox(settled, mn, mx);
    expectVec3Near(mn, glm::vec3(-1.96751f, 0.127029f, -0.657394f), 1e-4f);
    expectVec3Near(mx, glm::vec3(1.87213f, 0.343311f, 1.95867f), 1e-4f);
}

TEST(EvalE7, RepeatSettleCorpusDeterministicAndThreadInvariant) {
    pgg::RunParams t1, t2;
    t1.threads = 1;
    t2.threads = 2;
    pgg::RunResult a = pgg::runFile(kSettleCorpus, t1);
    pgg::RunResult b = pgg::runFile(kSettleCorpus, t2);
    pgg::RunResult again = pgg::runFile(kSettleCorpus, t2);
    expectNoErrors(a);
    expectNoErrors(b);
    expectNoErrors(again);
    EXPECT_EQ(geoContentHash(pgg::asGeo(*outputOf(a, "settled"))),
              geoContentHash(pgg::asGeo(*outputOf(b, "settled"))));
    EXPECT_EQ(geoContentHash(pgg::asGeo(*outputOf(b, "settled"))),
              geoContentHash(pgg::asGeo(*outputOf(again, "settled"))));
}

TEST(EvalE7, FractureCorpusMatchesGoldens) {
    pgg::RunParams p;
    p.threads = 4;
    pgg::RunResult r = pgg::runFile(kFractureCorpus, p);
    expectNoErrors(r);
    ASSERT_EQ(r.outputs.size(), 3u);
    const pgg::Geo& rock = *pgg::asGeo(*outputOf(r, "rock"));
    EXPECT_EQ(rock.pointCount(), 162u);
    EXPECT_EQ(rock.faceCount(), 320u);
    const pgg::Geo& fractured = *pgg::asGeo(*outputOf(r, "fractured"));
    EXPECT_EQ(fractured.pointCount(), 135860u);
    EXPECT_EQ(fractured.faceCount(), 271708u);
    glm::vec3 mn, mx;
    pgg::geoBBox(fractured, mn, mx);
    expectVec3Near(mn, glm::vec3(-0.997602f, -1.06918f, -1.10772f), 1e-4f);
    expectVec3Near(mx, glm::vec3(1.04371f, 0.983486f, 0.997078f), 1e-4f);
    // Three non-empty pieces: dense @island_id 0..2 on the faces.
    const std::vector<int64_t> ids = islandIds(fractured);
    ASSERT_EQ(ids.size(), fractured.faceCount());
    const std::unordered_set<int64_t> unique(ids.begin(), ids.end());
    EXPECT_EQ(unique.size(), 3u);
    // The foreach output keeps the piece split and the tag.
    const pgg::Geo& chunks = *pgg::asGeo(*outputOf(r, "chunks"));
    EXPECT_EQ(chunks.faceCount(), fractured.faceCount());
    const std::vector<pgg::GeoPtr> pieces = pgg::splitMeshPieces(chunks);
    EXPECT_EQ(pieces.size(), unique.size());
    for (const pgg::GeoPtr& piece : pieces) EXPECT_EQ(pgg::nonManifoldEdgeCount(*piece), 0u);
    pgg::geoBBox(chunks, mn, mx);
    expectVec3Near(mn, glm::vec3(-1.04315f, -1.06009f, -1.09909f), 1e-4f);
    expectVec3Near(mx, glm::vec3(1.03561f, 0.952295f, 1.03629f), 1e-4f);
}

TEST(EvalE7, FractureCorpusDeterministicAndThreadInvariant) {
    pgg::RunParams t2, t4;
    t2.threads = 2;
    t4.threads = 4;
    pgg::RunResult a = pgg::runFile(kFractureCorpus, t2);
    pgg::RunResult b = pgg::runFile(kFractureCorpus, t4);
    pgg::RunResult again = pgg::runFile(kFractureCorpus, t4);
    expectNoErrors(a);
    expectNoErrors(b);
    expectNoErrors(again);
    EXPECT_EQ(geoContentHash(pgg::asGeo(*outputOf(a, "chunks"))),
              geoContentHash(pgg::asGeo(*outputOf(b, "chunks"))));
    EXPECT_EQ(geoContentHash(pgg::asGeo(*outputOf(b, "chunks"))),
              geoContentHash(pgg::asGeo(*outputOf(again, "chunks"))));
}

}  // namespace
