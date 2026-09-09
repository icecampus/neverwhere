// E5 acceptance (spec §15, §16): the composition tower runs end-to-end
// verbatim — def calls inline through four composition levels, expect
// contracts hold, tap stays a no-op — with the golden structural fingerprints
// of the current numeric profile (src/tests/pgg/goldens/tower.fp, C3) and
// run-to-run reproducibility (N1).
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "goldens_utils.h"
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

    // Golden structural fingerprints of scene (the watertight merge of wall
    // and hero, no welding) and anchors (the poisson landing points on the
    // wall's flat tops): src/tests/pgg/goldens/tower.fp; re-record: PggTool
    // run src/tests/pgg/corpus/tower.pgg --param world_seed=42 --update-goldens.
    pggtest::expectGolden("tower", r);
    pgg::GeoPtr scene = pggtest::geoOutput(r, "scene");
    ASSERT_TRUE(scene != nullptr);
    ASSERT_EQ(scene->kind, pgg::GeoKind::Mesh);
    EXPECT_EQ(pgg::nonManifoldEdgeCount(*scene), 0u);
    pgg::GeoPtr anchors = pggtest::geoOutput(r, "anchors");
    ASSERT_TRUE(anchors != nullptr);
    ASSERT_EQ(anchors->kind, pgg::GeoKind::Points);

    // N1: a second run reproduces the world bit-for-bit (same seed, §5.2).
    pgg::RunResult r2 = pgg::runFile(kTower, towerParams());
    pggtest::expectNoErrors(r2);
    EXPECT_EQ(pggtest::geoContentHash(pggtest::geoOutput(r, "scene")),
              pggtest::geoContentHash(pggtest::geoOutput(r2, "scene")));
    EXPECT_EQ(pggtest::geoContentHash(pggtest::geoOutput(r, "anchors")),
              pggtest::geoContentHash(pggtest::geoOutput(r2, "anchors")));
}

}  // namespace
