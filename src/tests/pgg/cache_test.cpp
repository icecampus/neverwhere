// E3 criterion 4 (spec §15, §5.3, N3/N4): the cross-run content-addressed
// cache. (a) N3: a second run over the same cache is 100% hits for the
// requested outputs (nothing recomputes); (b) N4: appending a tail binding +
// output leaves the head 100% cached and misses only the tail; (c) a launch
// param change invalidates exactly the dependent chain; (d) LRU eviction at
// a small capacity. Plus fingerprint units: stability, tail/param/profile
// sensitivity of the structural hash itself.
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/pgg.h"
#include "pgg/src/eval/cache.h"
#include "pgg/src/eval/fingerprint.h"
#include "pgg/src/eval/profile.h"
#include "test_utils.h"

namespace {

pgg::RunParams withCache(pgg::MemoryCache& cache) {
    pgg::RunParams p;
    p.cache = &cache;
    return p;
}

// --- (a) N3: second run is 100% hits -----------------------------------------

const char* kHead =
    "a = box(size = (1, 1, 1))\n"
    "b = transform(a, translate = (2, 0, 0))\n"
    "c = merge(a, b)\n"
    "output b\n"
    "output c\n";

TEST(Cache, SecondRunIsAllHits) {
    pgg::MemoryCache cache;
    pgg::RunResult r1 = pgg::run(std::string(kHead), withCache(cache));
    pggtest::expectNoErrors(r1);
    EXPECT_EQ(r1.stats.cacheMisses, 3u);  // a, b, c (all value bindings)
    EXPECT_EQ(r1.stats.cacheHits, 0u);

    pgg::RunResult r2 = pgg::run(std::string(kHead), withCache(cache));
    pggtest::expectNoErrors(r2);
    EXPECT_EQ(r2.stats.cacheHits, 2u);  // the two requested outputs
    EXPECT_EQ(r2.stats.cacheMisses, 0u);
    EXPECT_EQ(r2.stats.fieldsEvaluated, 0u);  // nothing recomputed
    EXPECT_EQ(*pggtest::geoOutput(r1, "c")->positions, *pggtest::geoOutput(r2, "c")->positions);
}

TEST(Cache, CorpusSecondRunIsAllHits) {
    // N3 acceptance on the E2 corpus: identical result, zero recomputation.
    const std::string corpus = std::string(PGG_CORPUS_DIR) + "/e2_rock_scatter.pgg";
    pgg::MemoryCache cache;
    pgg::RunResult r1 = pgg::runFile(corpus, withCache(cache));
    pggtest::expectNoErrors(r1);
    EXPECT_GT(r1.stats.cacheMisses, 0u);

    pgg::RunResult r2 = pgg::runFile(corpus, withCache(cache));
    pggtest::expectNoErrors(r2);
    EXPECT_EQ(r2.stats.cacheMisses, 0u);
    // rock + inst + the shared bbox tuple node: 3 binding fingerprints hit.
    EXPECT_EQ(r2.stats.cacheHits, 3u);
    EXPECT_EQ(r2.stats.fieldsEvaluated, 0u);
    for (const char* out : {"rock", "inst"}) {
        EXPECT_EQ(pggtest::geoContentHash(pggtest::geoOutput(r1, out)),
                  pggtest::geoContentHash(pggtest::geoOutput(r2, out))) << out;
    }
}

// --- (b) N4: a tail edit keeps the head cached --------------------------------

TEST(Cache, TailAppendKeepsHeadCached) {
    pgg::MemoryCache cache;
    pgg::RunResult r1 = pgg::run(std::string(kHead), withCache(cache));
    pggtest::expectNoErrors(r1);

    const std::string tailed = std::string(kHead) +
        "d = transform(c, translate = (0, 5, 0))\n"
        "output d\n";
    pgg::RunResult r2 = pgg::run(tailed, withCache(cache));
    pggtest::expectNoErrors(r2);
    EXPECT_EQ(r2.stats.cacheHits, 2u);    // head outputs, 100%
    EXPECT_EQ(r2.stats.cacheMisses, 1u);  // only the tail binding
    EXPECT_EQ(r2.stats.fieldsEvaluated, 0u);
    // The head outputs are byte-identical to the first run.
    EXPECT_EQ(*pggtest::geoOutput(r1, "c")->positions, *pggtest::geoOutput(r2, "c")->positions);
    glm::vec3 mn, mx;
    pgg::geoBBox(*pggtest::geoOutput(r2, "d"), mn, mx);
    pggtest::expectF32Near(mn.y, 4.5f, 1e-5f);  // c spans y -0.5..0.5, +5
}

// --- (c) a param change invalidates exactly the dependent chain ---------------

const char* kParamSrc =
    "param dx: f32 = 0.0\n"
    "base = box(size = (1, 1, 1))\n"
    "moved = transform(base, translate = vec3(dx, 0, 0))\n"
    "other = ico_sphere(subdiv = 1, radius = 1.0)\n"
    "output moved\n"
    "output other\n";

TEST(Cache, ParamChangeInvalidatesDependentChainOnly) {
    pgg::MemoryCache cache;
    pgg::RunResult r1 = pgg::run(std::string(kParamSrc), withCache(cache));
    pggtest::expectNoErrors(r1);
    EXPECT_EQ(r1.stats.cacheMisses, 3u);  // base, moved, other
    glm::vec3 mn1, mx1;
    pgg::geoBBox(*pggtest::geoOutput(r1, "moved"), mn1, mx1);
    pggtest::expectF32Near(mn1.x, -0.5f, 1e-5f);

    pgg::RunParams p2 = withCache(cache);
    p2.values.push_back({"dx", pgg::Value(1.0f)});
    pgg::RunResult r2 = pgg::run(std::string(kParamSrc), p2);
    pggtest::expectNoErrors(r2);
    // Only `moved` depends on dx: it misses; base and other hit.
    EXPECT_EQ(r2.stats.cacheMisses, 1u);
    EXPECT_EQ(r2.stats.cacheHits, 2u);
    glm::vec3 mn2, mx2;
    pgg::geoBBox(*pggtest::geoOutput(r2, "moved"), mn2, mx2);
    pggtest::expectF32Near(mn2.x, 0.5f, 1e-5f);

    // Same param again: everything is cached.
    pgg::RunResult r3 = pgg::run(std::string(kParamSrc), p2);
    pggtest::expectNoErrors(r3);
    EXPECT_EQ(r3.stats.cacheMisses, 0u);
    EXPECT_EQ(r3.stats.cacheHits, 2u);
    // ...and the default-param run is still cached under its own fingerprint.
    pgg::RunResult r4 = pgg::run(std::string(kParamSrc), withCache(cache));
    EXPECT_EQ(r4.stats.cacheMisses, 0u);
    EXPECT_EQ(r4.stats.cacheHits, 2u);
}

// --- (d) LRU eviction at a small capacity --------------------------------------

const char* kBoxes =
    "a = box(size = (1, 1, 1))\n"
    "b = box(size = (2, 2, 2))\n"
    "c = box(size = (3, 3, 3))\n"
    "d = box(size = (4, 4, 4))\n"
    "output a\n"
    "output b\n"
    "output c\n"
    "output d\n";

TEST(Cache, LruEvictsLeastRecentlyUsed) {
    pgg::MemoryCache cache(3);
    pgg::RunResult r1 = pgg::run(std::string(kBoxes), withCache(cache), {"a", "b", "c"});
    pggtest::expectNoErrors(r1);
    EXPECT_EQ(r1.stats.cacheMisses, 3u);
    EXPECT_LE(cache.size(), 3u);

    // Refresh `a` so it is no longer the least recently used.
    pgg::RunResult r2 = pgg::run(std::string(kBoxes), withCache(cache), {"a"});
    EXPECT_EQ(r2.stats.cacheHits, 1u);

    // Insert d: evicts the LRU entry (b), not the refreshed a (FIFO would
    // have evicted a — this is what pins the policy as LRU).
    pgg::RunResult r3 = pgg::run(std::string(kBoxes), withCache(cache), {"d"});
    EXPECT_EQ(r3.stats.cacheMisses, 1u);
    EXPECT_LE(cache.size(), 3u);

    pgg::RunResult r4 = pgg::run(std::string(kBoxes), withCache(cache), {"a"});
    EXPECT_EQ(r4.stats.cacheHits, 1u);  // a survived
    pgg::RunResult r5 = pgg::run(std::string(kBoxes), withCache(cache), {"b"});
    EXPECT_EQ(r5.stats.cacheMisses, 1u);  // b was evicted
}

TEST(Cache, DefaultCapacityIsDocumentedValue) {
    pgg::MemoryCache cache;
    EXPECT_EQ(cache.capacity(), 512u);
}

// --- fingerprint units ----------------------------------------------------------

TEST(Fingerprint, StableAcrossInstances) {
    pgg::Document doc = pgg::parse(std::string(kHead));
    ASSERT_TRUE(doc.file != nullptr);
    pgg::BindingFingerprinter f1(*doc.file, {}, pgg::numericProfileId());
    pgg::BindingFingerprinter f2(*doc.file, {}, pgg::numericProfileId());
    for (const char* n : {"a", "b", "c"}) {
        EXPECT_NE(f1.fingerprint(n), 0ull) << n;
        EXPECT_EQ(f1.fingerprint(n), f2.fingerprint(n)) << n;
    }
}

TEST(Fingerprint, TailAppendKeepsHeadFingerprints) {
    pgg::Document head = pgg::parse(std::string(kHead));
    pgg::Document tailed = pgg::parse(std::string(kHead) +
                                      "d = transform(c, translate = (0, 5, 0))\n"
                                      "output d\n");
    ASSERT_TRUE(head.file && tailed.file);
    pgg::BindingFingerprinter fh(*head.file, {}, pgg::numericProfileId());
    pgg::BindingFingerprinter ft(*tailed.file, {}, pgg::numericProfileId());
    for (const char* n : {"a", "b", "c"}) EXPECT_EQ(fh.fingerprint(n), ft.fingerprint(n)) << n;
    EXPECT_NE(ft.fingerprint("d"), 0ull);
    EXPECT_NE(ft.fingerprint("d"), ft.fingerprint("b"));
}

TEST(Fingerprint, ParamValueChangesDependentFingerprint) {
    pgg::Document doc = pgg::parse(std::string(kParamSrc));
    ASSERT_TRUE(doc.file != nullptr);
    pgg::BindingFingerprinter def(*doc.file, {}, pgg::numericProfileId());
    pgg::BindingFingerprinter bound(*doc.file, {{"dx", pgg::Value(1.0f)}}, pgg::numericProfileId());
    EXPECT_NE(def.fingerprint("moved"), bound.fingerprint("moved"));  // depends on dx
    EXPECT_EQ(def.fingerprint("base"), bound.fingerprint("base"));    // independent
    EXPECT_EQ(def.fingerprint("other"), bound.fingerprint("other"));  // independent
}

TEST(Fingerprint, ProfileIdSeedsEveryFingerprint) {
    pgg::Document doc = pgg::parse(std::string(kHead));
    ASSERT_TRUE(doc.file != nullptr);
    pgg::BindingFingerprinter f1(*doc.file, {}, 111);
    pgg::BindingFingerprinter f2(*doc.file, {}, 222);
    EXPECT_NE(f1.fingerprint("a"), f2.fingerprint("a"));
}

TEST(Fingerprint, ValueHashIsStructural) {
    uint64_t h1 = 0, h2 = 0, h3 = 0;
    EXPECT_TRUE(pgg::fingerprintValue(pgg::Value(static_cast<int64_t>(42)), h1));
    EXPECT_TRUE(pgg::fingerprintValue(pgg::Value(static_cast<int64_t>(42)), h2));
    EXPECT_TRUE(pgg::fingerprintValue(pgg::Value(static_cast<int64_t>(43)), h3));
    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, h3);
    uint64_t hs = 0;
    EXPECT_TRUE(pgg::fingerprintValue(pgg::Value(std::string("rocks")), hs));
    EXPECT_NE(hs, h1);
    // A float hashes by bit pattern, not by formatting.
    uint64_t hf1 = 0, hf2 = 0;
    EXPECT_TRUE(pgg::fingerprintValue(pgg::Value(1.5f), hf1));
    EXPECT_TRUE(pgg::fingerprintValue(pgg::Value(1.5f), hf2));
    EXPECT_EQ(hf1, hf2);
}

// --- E5: per-instance invalidation through the flat expansion (§7.6) ---------

const char* kInstSrc =
    "def mk(size: f32) -> (out: geo<mesh>) {\n"
    "    \"\"\"One box.\"\"\"\n"
    "    out = box(size = vec3(size, size, size))\n"
    "}\n"
    "a = mk(size = 1.0)\n"
    "b = mk(size = 2.0)\n"
    "scene = merge(a, b)\n"
    "output scene\n";

TEST(Cache, ArgumentEditInvalidatesOneInstanceOnly) {
    // Editing the argument of call b must miss exactly b's instance chain
    // (size param, out, alias) and the merge; call a's chain stays cached.
    pgg::MemoryCache cache;
    pgg::RunResult r1 = pgg::run(std::string(kInstSrc), withCache(cache));
    pggtest::expectNoErrors(r1);
    EXPECT_EQ(r1.stats.cacheMisses, 7u);  // mk[0].size/.out, a, mk[1].size/.out, b, scene

    const std::string edited =
        "def mk(size: f32) -> (out: geo<mesh>) {\n"
        "    \"\"\"One box.\"\"\"\n"
        "    out = box(size = vec3(size, size, size))\n"
        "}\n"
        "a = mk(size = 1.0)\n"
        "b = mk(size = 3.0)\n"
        "scene = merge(a, b)\n"
        "output scene\n";
    pgg::RunResult r2 = pgg::run(edited, withCache(cache));
    pggtest::expectNoErrors(r2);
    // The alias `a` hits and its hit covers the whole mk[0] subtree (it is
    // never pulled); only b's chain (size param, out, alias) and the merge miss.
    EXPECT_EQ(r2.stats.cacheHits, 1u);    // a
    EXPECT_EQ(r2.stats.cacheMisses, 4u);  // scene, b, mk[1].out, mk[1].size
    EXPECT_EQ(r2.stats.fieldsEvaluated, 0u);
}

TEST(Cache, DefBodyEditInvalidatesOnlyItsInstances) {
    const std::string src1 =
        "def ma(size: f32) -> (out: geo<mesh>) {\n"
        "    \"\"\"A.\"\"\"\n"
        "    out = box(size = vec3(size, size, size))\n"
        "}\n"
        "def mb(size: f32) -> (out: geo<mesh>) {\n"
        "    \"\"\"B.\"\"\"\n"
        "    out = box(size = vec3(size, size, size))\n"
        "}\n"
        "a = ma(1.0)\n"
        "b = mb(2.0)\n"
        "scene = merge(a, b)\n"
        "output scene\n";
    pgg::MemoryCache cache;
    pgg::RunResult r1 = pgg::run(src1, withCache(cache));
    pggtest::expectNoErrors(r1);
    EXPECT_EQ(r1.stats.cacheMisses, 7u);

    // Edit ma's body only: ma[0].out/a/scene miss; mb's whole chain and the
    // unchanged size-parameter bindings hit.
    const std::string src2 =
        "def ma(size: f32) -> (out: geo<mesh>) {\n"
        "    \"\"\"A.\"\"\"\n"
        "    box0 = box(size = vec3(size, size, size))\n"
        "    out = transform(box0, translate = (10, 0, 0))\n"
        "}\n"
        "def mb(size: f32) -> (out: geo<mesh>) {\n"
        "    \"\"\"B.\"\"\"\n"
        "    out = box(size = vec3(size, size, size))\n"
        "}\n"
        "a = ma(1.0)\n"
        "b = mb(2.0)\n"
        "scene = merge(a, b)\n"
        "output scene\n";
    pgg::RunResult r2 = pgg::run(src2, withCache(cache));
    pggtest::expectNoErrors(r2);
    // `b` hits and covers mb's whole subtree; ma[0].box0 hits too — its
    // structural fingerprint is exactly src1's cached ma[0].out (the box did
    // not change, only a transform was added after it). That hit covers
    // ma[0].size, which is therefore never pulled.
    EXPECT_EQ(r2.stats.cacheHits, 2u);    // b, ma[0].box0
    EXPECT_EQ(r2.stats.cacheMisses, 3u);  // scene, a, ma[0].out
}

TEST(Cache, DefRunSecondTimeIsAllHits) {
    // N3 on instances: a second run of the same file is 100% hits.
    pgg::MemoryCache cache;
    pgg::RunResult r1 = pgg::run(std::string(kInstSrc), withCache(cache));
    pggtest::expectNoErrors(r1);
    pgg::RunResult r2 = pgg::run(std::string(kInstSrc), withCache(cache));
    pggtest::expectNoErrors(r2);
    EXPECT_EQ(r2.stats.cacheMisses, 0u);
    EXPECT_EQ(r2.stats.cacheHits, 1u);  // only the requested output is pulled
    EXPECT_EQ(r2.stats.fieldsEvaluated, 0u);
    EXPECT_EQ(*pggtest::geoOutput(r1, "scene")->positions, *pggtest::geoOutput(r2, "scene")->positions);
}

}  // namespace
