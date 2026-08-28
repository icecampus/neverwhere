// RNG tests (spec §5.2): determinism, split independence, alias identity,
// counter/lane addressing, golden word values (regression pin for the v0
// draft algorithm; cross-platform vectors are stage E3).
#include <gtest/gtest.h>

#include "pgg/src/eval/rng.h"

namespace {

TEST(Rng, FromSeedIsDeterministic) {
    EXPECT_EQ(pgg::rngFromSeed(42), pgg::rngFromSeed(42));
    EXPECT_NE(pgg::rngFromSeed(42), pgg::rngFromSeed(43));
}

TEST(Rng, FromSeedGoldenKeys) {
    const pgg::Rng r = pgg::rngFromSeed(42);
    EXPECT_EQ(r.lo, 15595420190114929605ull);
    EXPECT_EQ(r.hi, 9466069698620324344ull);
    const pgg::Rng r0 = pgg::rngFromSeed(0);
    EXPECT_EQ(r0.lo, 7192185014346937746ull);
    EXPECT_EQ(r0.hi, 6377179373001286278ull);
}

TEST(Rng, SplitIsPureAndIndependent) {
    const pgg::Rng parent = pgg::rngFromSeed(42);
    const pgg::Rng a = pgg::splitRng(parent, static_cast<int64_t>(7));
    // Same parent + same key -> same child; parent unchanged (purity).
    EXPECT_EQ(a, pgg::splitRng(parent, static_cast<int64_t>(7)));
    EXPECT_EQ(parent, pgg::rngFromSeed(42));
    // Different keys -> independent children.
    EXPECT_NE(a, pgg::splitRng(parent, static_cast<int64_t>(8)));
    // Domain separation: int key 7 and string key "7" live apart.
    EXPECT_NE(a, pgg::splitRng(parent, "7"));
    EXPECT_NE(pgg::splitRng(parent, "rocks"), pgg::splitRng(parent, "surface"));
    // Children produce independent streams.
    EXPECT_NE(pgg::rngWord(a, 0, 0), pgg::rngWord(pgg::splitRng(parent, static_cast<int64_t>(8)), 0, 0));
}

TEST(Rng, SplitGoldenKeys) {
    const pgg::Rng parent = pgg::rngFromSeed(42);
    const pgg::Rng si = pgg::splitRng(parent, static_cast<int64_t>(7));
    EXPECT_EQ(si.lo, 5440007216246681111ull);
    EXPECT_EQ(si.hi, 9883383544179121069ull);
    const pgg::Rng ss = pgg::splitRng(parent, "surface");
    EXPECT_EQ(ss.lo, 4808704777131418887ull);
    EXPECT_EQ(ss.hi, 5352791904761786260ull);
}

TEST(Rng, AliasIsIdentical) {
    const pgg::Rng r = pgg::splitRng(pgg::rngFromSeed(42), "shared");
    EXPECT_EQ(pgg::aliasRng(r), r);
    EXPECT_EQ(pgg::rngWord(pgg::aliasRng(r), 5, 2), pgg::rngWord(r, 5, 2));
}

TEST(Rng, CounterLaneAddressing) {
    const pgg::Rng r = pgg::rngFromSeed(42);
    // Addressed, not sequential: same tuple -> same word, always.
    EXPECT_EQ(pgg::rngWord(r, 0, 0), pgg::rngWord(r, 0, 0));
    // Counter and lane both select different words.
    EXPECT_NE(pgg::rngWord(r, 0, 0), pgg::rngWord(r, 1, 0));
    EXPECT_NE(pgg::rngWord(r, 0, 0), pgg::rngWord(r, 0, 1));
    EXPECT_NE(pgg::rngWord(r, 1, 0), pgg::rngWord(r, 0, 1));
    EXPECT_NE(pgg::rngWord(r, 0, 1), pgg::rngWord(r, 0, 2));
}

TEST(Rng, GoldenWords) {
    const pgg::Rng r = pgg::rngFromSeed(42);
    EXPECT_EQ(pgg::rngWord(r, 0, 0), 3035417490u);
    EXPECT_EQ(pgg::rngWord(r, 1, 0), 991493056u);
    EXPECT_EQ(pgg::rngWord(r, 0, 1), 3776494060u);
    EXPECT_EQ(pgg::rngWord(r, 0, 2), 2314360696u);
    EXPECT_EQ(pgg::rngWord(r, 12345, 7), 2542945776u);
    const pgg::Rng ss = pgg::splitRng(r, "surface");
    EXPECT_EQ(pgg::rngWord(ss, 0, 0), 969204098u);
}

TEST(Rng, WordToF32Conversion) {
    // Top 24 bits over 2^24: [0, 1).
    EXPECT_FLOAT_EQ(pgg::rngWordToF32(0u), 0.0f);
    EXPECT_FLOAT_EQ(pgg::rngWordToF32(0xFFFFFFFFu), 16777215.0f / 16777216.0f);
    EXPECT_FLOAT_EQ(pgg::rngWordToF32(0x80000000u), 0.5f);
    const pgg::Rng r = pgg::rngFromSeed(42);
    EXPECT_FLOAT_EQ(pgg::rngF32(r, 0, 0), 0.706738174f);
}

TEST(Rng, NoiseIsDeterministicAndRanged) {
    const pgg::Rng r = pgg::splitRng(pgg::rngFromSeed(42), "surface");
    EXPECT_FLOAT_EQ(pgg::valueNoise(r, 1.5f, 2.5f, 3.5f, 0), 0.65052164f);
    EXPECT_FLOAT_EQ(pgg::fbmNoise(r, 1.5f, 2.5f, 3.5f, 5, 2.0f, 0.5f), -0.054200567f);
    float mn = 1e9f, mx = -1e9f;
    for (int i = 0; i < 50000; ++i) {
        const float v = pgg::fbmNoise(r, i * 0.137f, i * 0.031f, i * 0.213f, 5, 2.0f, 0.5f);
        mn = std::min(mn, v);
        mx = std::max(mx, v);
        const float u = pgg::valueNoise(r, i * 0.17f, i * 0.11f, i * 0.07f, 1);
        EXPECT_GE(u, 0.0f);
        EXPECT_LT(u, 1.0f);
    }
    // Signed, normalized fbm stays in [-1, 1].
    EXPECT_GE(mn, -1.0f);
    EXPECT_LE(mx, 1.0f);
    // ...and actually uses both signs (a real spread, not a constant).
    EXPECT_LT(mn, -0.5f);
    EXPECT_GT(mx, 0.5f);
}

}  // namespace
