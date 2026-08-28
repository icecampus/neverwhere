#include "../../pch.h"

#include "rng.h"

#include <cmath>

namespace pgg {
namespace {

// splitmix64 finalizer: full 64-bit avalanche, used for key derivation.
uint64_t mix64(uint64_t x) {
    x += 0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    return x ^ (x >> 31);
}

uint64_t rotl64(uint64_t x, int k) { return (x << k) | (x >> (64 - k)); }

// pcg3d integer hash: three 32-bit words in, three out. Same mixing family as
// the stone_gen hash, but pgg's own instance (greenfield, spec §18): no
// sin-hash decorrelation, no directional stripes on higher octaves.
void pcg3d(uint32_t& x, uint32_t& y, uint32_t& z) {
    x = x * 1664525u + 1013904223u;
    y = y * 1664525u + 1013904223u;
    z = z * 1664525u + 1013904223u;
    x += y * z;
    y += z * x;
    z += x * y;
    x ^= x >> 16u;
    y ^= y >> 16u;
    z ^= z >> 16u;
    x += y * z;
    y += z * x;
    z += x * y;
}

uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

// Shared split combiner: the domain-separated key hash decides the namespace
// (int keys vs string keys), the parent key is folded in with avalanche.
Rng splitMixed(Rng parent, uint64_t keyHash) {
    const uint64_t lo = mix64(parent.lo ^ keyHash);
    const uint64_t hi = mix64(parent.hi ^ rotl64(keyHash, 31) ^ lo);
    return {lo, hi};
}

float smooth01(float t) { return t * t * (3.0f - 2.0f * t); }

}  // namespace

Rng rngFromSeed(int64_t seed) {
    const uint64_t z = static_cast<uint64_t>(seed);
    const uint64_t lo = mix64(z ^ 0x6a09e667f3bcc909ull);
    const uint64_t hi = mix64(lo ^ 0xbb67ae8584caa73bull);
    return {lo, hi};
}

Rng splitRng(Rng parent, int64_t key) {
    // Domain separation: int-key splits live in their own namespace.
    return splitMixed(parent, mix64(static_cast<uint64_t>(key) ^ 0x3c79ac492ba7b653ull));
}

Rng splitRng(Rng parent, const std::string& key) {
    return splitMixed(parent, mix64(fnv1a64(key) ^ 0x1c69b3f74ac4ae35ull));
}

Rng aliasRng(Rng parent) { return parent; }

uint32_t rngWord(Rng rng, uint64_t counter, uint32_t lane) {
    uint32_t x = static_cast<uint32_t>(rng.lo) ^ static_cast<uint32_t>(counter);
    uint32_t y = static_cast<uint32_t>(rng.lo >> 32) ^ static_cast<uint32_t>(counter >> 32);
    uint32_t z = static_cast<uint32_t>(rng.hi) ^ lane;
    pcg3d(x, y, z);
    x ^= static_cast<uint32_t>(rng.hi >> 32);
    pcg3d(x, y, z);
    return x;
}

float rngWordToF32(uint32_t word) {
    return static_cast<float>(word >> 8) * (1.0f / 16777216.0f);
}

float rngF32(Rng rng, uint64_t counter, uint32_t lane) {
    return rngWordToF32(rngWord(rng, counter, lane));
}

float noiseCell(Rng rng, int32_t cx, int32_t cy, int32_t cz, uint32_t lane) {
    uint32_t x = static_cast<uint32_t>(cx) ^ static_cast<uint32_t>(rng.lo);
    uint32_t y = static_cast<uint32_t>(cy) ^ static_cast<uint32_t>(rng.lo >> 32);
    uint32_t z = static_cast<uint32_t>(cz) ^ static_cast<uint32_t>(rng.hi);
    pcg3d(x, y, z);
    x ^= static_cast<uint32_t>(rng.hi >> 32) ^ lane;
    pcg3d(x, y, z);
    return rngWordToF32(x);
}

float valueNoise(Rng rng, float x, float y, float z, uint32_t lane) {
    const int32_t ix = static_cast<int32_t>(std::floor(x));
    const int32_t iy = static_cast<int32_t>(std::floor(y));
    const int32_t iz = static_cast<int32_t>(std::floor(z));
    const float fx = smooth01(x - static_cast<float>(ix));
    const float fy = smooth01(y - static_cast<float>(iy));
    const float fz = smooth01(z - static_cast<float>(iz));
    const float n000 = noiseCell(rng, ix, iy, iz, lane);
    const float n100 = noiseCell(rng, ix + 1, iy, iz, lane);
    const float n010 = noiseCell(rng, ix, iy + 1, iz, lane);
    const float n110 = noiseCell(rng, ix + 1, iy + 1, iz, lane);
    const float n001 = noiseCell(rng, ix, iy, iz + 1, lane);
    const float n101 = noiseCell(rng, ix + 1, iy, iz + 1, lane);
    const float n011 = noiseCell(rng, ix, iy + 1, iz + 1, lane);
    const float n111 = noiseCell(rng, ix + 1, iy + 1, iz + 1, lane);
    const float nx00 = n000 + (n100 - n000) * fx;
    const float nx10 = n010 + (n110 - n010) * fx;
    const float nx01 = n001 + (n101 - n001) * fx;
    const float nx11 = n011 + (n111 - n011) * fx;
    const float nxy0 = nx00 + (nx10 - nx00) * fy;
    const float nxy1 = nx01 + (nx11 - nx01) * fy;
    return nxy0 + (nxy1 - nxy0) * fz;
}

float fbmNoise(Rng rng, float x, float y, float z, int octaves, float lacunarity, float gain) {
    float sum = 0.0f;
    float amp = 0.5f;
    float norm = 0.0f;
    for (int o = 0; o < octaves; ++o) {
        // Octave index as the noise lane: octaves are domain-separated.
        sum += amp * (2.0f * valueNoise(rng, x, y, z, static_cast<uint32_t>(o)) - 1.0f);
        norm += amp;
        x *= lacunarity;
        y *= lacunarity;
        z *= lacunarity;
        amp *= gain;
    }
    return norm > 0.0f ? sum / norm : 0.0f;
}

}  // namespace pgg
