#pragma once

// Shared helpers for the pgg test suites (E3): epsilon comparisons for float
// goldens (criterion: between numeric profiles floats agree only by
// tolerance) and a content hash over geometry position/topology bytes (legal
// as an exact comparison within one profile — used for the thread-count
// invariance and run-to-run reproducibility criteria, N1/N7).

#include <cstdint>
#include <cstring>

#include <gtest/gtest.h>

#include "pgg/eval.h"

namespace pggtest {

inline void expectNoErrors(const pgg::RunResult& r) {
    for (const pgg::Diagnostic& d : r.diagnostics)
        if (!d.isWarning) ADD_FAILURE() << d.code << " " << d.message;
    EXPECT_FALSE(r.hasErrors());
}

inline pgg::GeoPtr geoOutput(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return pgg::asGeo(o.value);
    return nullptr;
}

inline const pgg::Value* outputOf(const pgg::RunResult& r, const std::string& name) {
    for (const auto& o : r.outputs)
        if (o.name == name) return &o.value;
    return nullptr;
}

// --- epsilon comparisons (float goldens are tolerances, not bit contracts) ---

inline void expectF32Near(float a, float b, float eps = 1e-5f) { EXPECT_NEAR(a, b, eps); }

inline void expectVec3Near(const glm::vec3& a, const glm::vec3& b, float eps = 1e-5f) {
    EXPECT_NEAR(a.x, b.x, eps);
    EXPECT_NEAR(a.y, b.y, eps);
    EXPECT_NEAR(a.z, b.z, eps);
}

// --- geometry content hash ---------------------------------------------------

inline void hashBytes(uint64_t& h, const void* data, size_t size) {
    const auto* p = static_cast<const unsigned char*>(data);
    for (size_t i = 0; i < size; ++i) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
}

template <typename T>
void hashVec(uint64_t& h, const std::vector<T>& v) {
    hashBytes(h, v.data(), v.size() * sizeof(T));
}

// Hash over the kind, point positions, normals and topology bytes (attribute
// payloads excluded on purpose: positions/topology are what the thread-count
// and reproducibility criteria pin).
inline uint64_t geoContentHash(const pgg::Geo& g) {
    uint64_t h = 14695981039346656037ull;
    hashBytes(h, &g.kind, sizeof(g.kind));
    if (g.positions) hashVec(h, *g.positions);
    const uint8_t hasNormals = g.normals ? 1 : 0;
    hashBytes(h, &hasNormals, 1);
    if (g.normals) hashVec(h, *g.normals);
    if (g.cornerVerts) hashVec(h, *g.cornerVerts);
    if (g.faceOffsets) hashVec(h, *g.faceOffsets);
    return h;
}

inline uint64_t geoContentHash(const pgg::GeoPtr& g) { return geoContentHash(*g); }

}  // namespace pggtest
