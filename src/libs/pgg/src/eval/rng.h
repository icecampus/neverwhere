#pragma once

// Reproducible counter-based PRNG (spec §5.2). An `rng` value is an immutable
// 128-bit generator key; random words are addressed by the tuple
// (generator_key, counter, lane) — never by sequential state, so parallel
// evaluation order cannot change the stream. Splitting is pure: same parent
// and same key always yield the same child, and the parent is unchanged.
//
// Algorithm pinned as "v0 draft" (spec §19): splitmix64-style 64-bit mixing
// for key derivation, a pcg3d two-round 32-bit hash for word generation, and
// word -> f32 via the top 24 bits. The full bit-for-bit cross-platform
// contract is stage E3.

#include <cstdint>
#include <string>

namespace pgg {

// Opaque 128-bit generator key (spec §4.5: rng is an immutable value, not a
// number and not an attribute).
struct Rng {
    uint64_t lo = 0;
    uint64_t hi = 0;
    bool operator==(const Rng&) const = default;
};

Rng rngFromSeed(int64_t seed);
Rng splitRng(Rng parent, int64_t key);
Rng splitRng(Rng parent, const std::string& key);
Rng aliasRng(Rng parent);  // identity — the lint-level alias (§8.5)

// Random word addressed by (generator_key, counter, lane).
uint32_t rngWord(Rng rng, uint64_t counter, uint32_t lane);

// word -> f32: top 24 bits / 2^24, range [0, 1).
float rngWordToF32(uint32_t word);
float rngF32(Rng rng, uint64_t counter, uint32_t lane);

// Lattice noise on the same generator: hash of one integer cell -> [0, 1).
float noiseCell(Rng rng, int32_t x, int32_t y, int32_t z, uint32_t lane);

// Trilinear value noise in [0, 1) (smoothstep-interpolated cell hashes).
float valueNoise(Rng rng, float x, float y, float z, uint32_t lane);

// Signed fbm in [-1, 1]: octaves of value noise with lacunarity/gain,
// amplitudes normalized so the result stays in range.
float fbmNoise(Rng rng, float x, float y, float z, int octaves, float lacunarity, float gain);

}  // namespace pgg
