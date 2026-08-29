// E3 criterion 1 (spec §15, §5.2, N1): the RNG contract is integer-exact and
// platform-independent. These tables pin the final v1 algorithm (spec §19):
// generator keys of rng_from_seed for fixed seeds, split_rng child keys for
// int and string keys (incl. chained splits), random words addressed by
// (rng, counter, lane) as exact hex, and the word -> f32 mapping as exact
// IEEE bit patterns (top 24 bits / 2^24 is exact in every IEEE f32
// environment). Float *sampling* (noise/fbm interpolation) is not part of
// the bit contract — those goldens use tolerances.
//
// N1 scenario: two runs of the E2 acceptance corpus produce an identical
// geometry content hash.
#include <bit>

#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/src/eval/profile.h"
#include "pgg/src/eval/rng.h"
#include "test_utils.h"

namespace {

TEST(Repro, RngFromSeedGoldenKeys) {
    struct V {
        int64_t seed;
        uint64_t lo, hi;
    };
    for (const V& v : {V{0, 7192185014346937746ull, 6377179373001286278ull},
                       V{1, 1927618558350093866ull, 3673427124908853601ull},
                       V{42, 15595420190114929605ull, 9466069698620324344ull},
                       V{-1, 2932223646667407290ull, 4248423641866101443ull},
                       V{1099511627776ll, 11583850074433214443ull, 15981359066869267224ull}}) {  // 2^40
        const pgg::Rng r = pgg::rngFromSeed(v.seed);
        EXPECT_EQ(r.lo, v.lo) << "seed " << v.seed;
        EXPECT_EQ(r.hi, v.hi) << "seed " << v.seed;
    }
}

TEST(Repro, SplitRngGoldenKeys) {
    const pgg::Rng root = pgg::rngFromSeed(42);
    // Domain separation: int keys and string keys are distinct namespaces.
    const pgg::Rng si = pgg::splitRng(root, static_cast<int64_t>(7));
    EXPECT_EQ(si.lo, 5440007216246681111ull);
    EXPECT_EQ(si.hi, 9883383544179121069ull);
    const pgg::Rng ss = pgg::splitRng(root, "surface");
    EXPECT_EQ(ss.lo, 4808704777131418887ull);
    EXPECT_EQ(ss.hi, 5352791904761786260ull);
    // Chained splits (hierarchical chunk-style derivation).
    const pgg::Rng chainA = pgg::splitRng(si, "detail");
    EXPECT_EQ(chainA.lo, 11199587163371238113ull);
    EXPECT_EQ(chainA.hi, 15283222829207235774ull);
    const pgg::Rng chainB = pgg::splitRng(ss, static_cast<int64_t>(13));
    EXPECT_EQ(chainB.lo, 17926280617444149296ull);
    EXPECT_EQ(chainB.hi, 12907453700942671592ull);
}

TEST(Repro, GoldenWordsExactHex) {
    const pgg::Rng root = pgg::rngFromSeed(42);
    EXPECT_EQ(pgg::rngWord(root, 0, 0), 0xb4eccb92u);
    EXPECT_EQ(pgg::rngWord(root, 1, 0), 0x3b18fbc0u);
    EXPECT_EQ(pgg::rngWord(root, 0, 1), 0xe118b9ecu);
    EXPECT_EQ(pgg::rngWord(root, 0, 2), 0x89f25778u);
    EXPECT_EQ(pgg::rngWord(root, 12345, 7), 0x979245f0u);
    const pgg::Rng ss = pgg::splitRng(root, "surface");
    EXPECT_EQ(pgg::rngWord(ss, 0, 0), 0x39c4e182u);
    const pgg::Rng si = pgg::splitRng(root, static_cast<int64_t>(7));
    EXPECT_EQ(pgg::rngWord(si, 0, 0), 0x41d7b1eeu);
    EXPECT_EQ(pgg::rngWord(si, 3, 5), 0x4b0afa85u);
    EXPECT_EQ(pgg::rngWord(pgg::splitRng(si, "detail"), 0, 0), 0xe6c6b566u);
    EXPECT_EQ(pgg::rngWord(pgg::splitRng(ss, static_cast<int64_t>(13)), 2, 1), 0xe9656be8u);
}

TEST(Repro, WordToF32ExactBitPatterns) {
    // Top 24 bits over 2^24: both the shift and the division are IEEE-exact,
    // so the bit patterns are part of the cross-platform contract.
    EXPECT_EQ(std::bit_cast<uint32_t>(pgg::rngWordToF32(0u)), 0x00000000u);
    EXPECT_EQ(std::bit_cast<uint32_t>(pgg::rngWordToF32(0xFFFFFFFFu)), 0x3f7fffffu);
    EXPECT_EQ(std::bit_cast<uint32_t>(pgg::rngWordToF32(0x80000000u)), 0x3f000000u);
    EXPECT_EQ(std::bit_cast<uint32_t>(pgg::rngWordToF32(0xB5020692u)), 0x3f350206u);
    const pgg::Rng root = pgg::rngFromSeed(42);
    EXPECT_EQ(std::bit_cast<uint32_t>(pgg::rngF32(root, 0, 0)), 0x3f34eccbu);
    EXPECT_EQ(std::bit_cast<uint32_t>(pgg::rngF32(root, 1, 0)), 0x3e6c63ecu);
    const pgg::Rng ss = pgg::splitRng(root, "surface");
    EXPECT_EQ(std::bit_cast<uint32_t>(pgg::rngF32(ss, 0, 0)), 0x3e671384u);
}

TEST(Repro, NoiseSamplesAreTolerancesNotBitContract) {
    // Float interpolation is outside the bit contract: pinned with an eps.
    const pgg::Rng ss = pgg::splitRng(pgg::rngFromSeed(42), "surface");
    pggtest::expectF32Near(pgg::valueNoise(ss, 1.5f, 2.5f, 3.5f, 0), 0.65052164f, 1e-6f);
    pggtest::expectF32Near(pgg::fbmNoise(ss, 1.5f, 2.5f, 3.5f, 5, 2.0f, 0.5f), -0.054200567f, 1e-6f);
}

TEST(Repro, NumericProfileIsStableAndComposed) {
    EXPECT_EQ(pgg::numericProfileId(), pgg::numericProfileId());
    EXPECT_NE(pgg::numericProfileId(), 0ull);
    const std::string s = pgg::numericProfileString();
    EXPECT_NE(s.find("pgg-abi="), std::string::npos);
    EXPECT_NE(s.find("rng="), std::string::npos);
    EXPECT_NE(s.find("isa="), std::string::npos);
}

TEST(Repro, CorpusRunsHaveIdenticalContentHash) {
    // N1: two runs of the E2 acceptance corpus, identical content hash.
    const std::string corpus = std::string(PGG_CORPUS_DIR) + "/e2_rock_scatter.pgg";
    pgg::RunResult a = pgg::runFile(corpus);
    pgg::RunResult b = pgg::runFile(corpus);
    pggtest::expectNoErrors(a);
    pggtest::expectNoErrors(b);
    for (const char* out : {"rock", "inst"}) {
        EXPECT_EQ(pggtest::geoContentHash(pggtest::geoOutput(a, out)),
                  pggtest::geoContentHash(pggtest::geoOutput(b, out))) << out;
    }
    // Runs report the numeric profile they executed in (spec §5.2).
    EXPECT_EQ(a.stats.profileId, pgg::numericProfileId());
    EXPECT_EQ(a.stats.profileId, b.stats.profileId);
}

}  // namespace
