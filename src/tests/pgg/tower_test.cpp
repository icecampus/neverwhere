// E5 acceptance (spec §15, §16): the composition tower runs end-to-end
// verbatim — def calls inline through four composition levels, expect
// contracts hold, tap stays a no-op — with golden counts/bbox/watertight of
// the current numeric profile and run-to-run reproducibility (N1).
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "test_utils.h"

namespace {

const std::string kTower = std::string(PGG_CORPUS_DIR) + "/tower.pgg";

pgg::RunParams towerParams() {
    pgg::RunParams p;
    p.values.push_back({"world_seed", pgg::Value(static_cast<int64_t>(42))});
    p.threads = 8;  // bounded Debug wall time; results are thread-invariant (N7)
    return p;
}

TEST(Tower, CorpusMatchesGoldensAndReproduces) {
    pgg::RunResult r = pgg::runFile(kTower, towerParams());
    pggtest::expectNoErrors(r);
    ASSERT_EQ(r.outputs.size(), 2u);

    // scene = merge(wall, hero): two watertight parts, no welding.
    pgg::GeoPtr scene = pggtest::geoOutput(r, "scene");
    ASSERT_TRUE(scene != nullptr);
    ASSERT_EQ(scene->kind, pgg::GeoKind::Mesh);
    EXPECT_EQ(scene->pointCount(), 359196u);
    EXPECT_EQ(scene->cornerCount(), 2154984u);
    EXPECT_EQ(scene->faceCount(), 718328u);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*scene), 0u);
    glm::vec3 mn, mx;
    pgg::geoBBox(*scene, mn, mx);
    // Bbox goldens were recorded through the PggTool %g print (6 significant
    // digits) — at magnitudes ~123 that is ~5e-4, hence the 1e-3 tolerance.
    pggtest::expectVec3Near(mn, glm::vec3(-4.15436f, -6.10666f, -2.94878f), 1e-3f);
    pggtest::expectVec3Near(mx, glm::vec3(18.1502f, 6.2129f, 123.022f), 1e-3f);

    // anchors: the poisson landing points on the wall's flat tops.
    pgg::GeoPtr anchors = pggtest::geoOutput(r, "anchors");
    ASSERT_TRUE(anchors != nullptr);
    ASSERT_EQ(anchors->kind, pgg::GeoKind::Points);
    EXPECT_EQ(anchors->pointCount(), 146u);
    glm::vec3 amn, amx;
    pgg::geoBBox(*anchors, amn, amx);
    pggtest::expectVec3Near(amn, glm::vec3(-2.36365f, -2.81133f, 1.90485f), 1e-3f);
    pggtest::expectVec3Near(amx, glm::vec3(2.47599f, 3.29271f, 122.995f), 1e-3f);

    // N1: a second run reproduces the world bit-for-bit (same seed, §5.2).
    pgg::RunResult r2 = pgg::runFile(kTower, towerParams());
    pggtest::expectNoErrors(r2);
    EXPECT_EQ(pggtest::geoContentHash(pggtest::geoOutput(r, "scene")),
              pggtest::geoContentHash(pggtest::geoOutput(r2, "scene")));
    EXPECT_EQ(pggtest::geoContentHash(pggtest::geoOutput(r, "anchors")),
              pggtest::geoContentHash(pggtest::geoOutput(r2, "anchors")));
}

}  // namespace
