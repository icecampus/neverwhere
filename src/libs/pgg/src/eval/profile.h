#pragma once

// Numeric profile (spec §5.2, §12.5): the identity of the numerical
// environment a run executes in. It enters the run report (RunStats) and the
// cache key — every structural fingerprint chains from profileId as its seed,
// so a profile change invalidates the whole content-addressed cache (N3).
//
// The profile string is composed from compile-time facts only: compiler and
// version, target ISA, significant FP flags, and the pinned versions of the
// pgg eval ABI and the RNG/noise algorithms. Within one profile all float
// results reproduce bit-for-bit (including under a changed thread count, N1);
// across profiles comparisons go by tolerances and invariants.

#include <cstdint>
#include <string>

namespace pgg {

// Version pins; bump when the corresponding contract changes.
inline constexpr unsigned kPggAbiVersion = 3;        // eval ABI (stage E3)
inline constexpr unsigned kRngAlgorithmVersion = 1;  // rng_from_seed/split_rng/word/f32 contract (final, spec §19 v1.0)
inline constexpr unsigned kNoiseAlgorithmVersion = 1;  // lattice/value/fbm noise sampling

// Human-readable composition ("pgg-abi=3;rng=1;noise=1;cc=...;isa=...;fp=...").
std::string numericProfileString();

// 64-bit hash of the profile string; the seed of every cache fingerprint.
uint64_t numericProfileId();

}  // namespace pgg
