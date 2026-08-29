#include "../../pch.h"

#include "profile.h"

namespace pgg {
namespace {

#define PGG_STR_(x) #x
#define PGG_STR(x) PGG_STR_(x)

const char* compilerTag() {
#if defined(__clang__)
    return "clang " __clang_version__;
#elif defined(_MSC_VER)
    return "msvc " PGG_STR(_MSC_VER);
#elif defined(__GNUC__)
    return "gcc " __VERSION__;
#else
    return "unknown";
#endif
}

const char* isaTag() {
#if defined(__x86_64__) || defined(_M_X64)
    return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    return "aarch64";
#elif defined(__wasm__) || defined(__EMSCRIPTEN__)
    return "wasm32";
#elif defined(__i386__) || defined(_M_IX86)
    return "x86";
#else
    return "unknown";
#endif
}

// FP flags that can change float results bit-wise. __FLT_EVAL_METHOD__ pins
// the x87/SSE excess-precision mode; fast-math and FMA contraction change
// rounding directly.
std::string fpTag() {
    std::string s;
#ifdef __FAST_MATH__
    s += "fast-math;";
#endif
#ifdef __FP_FAST_FMA
    s += "fast-fma;";
#endif
#ifdef __FP_FAST_FMAF
    s += "fast-fmaf;";
#endif
#ifdef __FLT_EVAL_METHOD__
    s += "flt-eval=" PGG_STR(__FLT_EVAL_METHOD__) ";";
#endif
#ifdef __SSE_MATH__
    s += "sse-math;";
#endif
    if (s.empty()) s = "strict;";
    return s;
}

uint64_t fnv1a64(const std::string& s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    return h;
}

}  // namespace

std::string numericProfileString() {
    static const std::string kProfile = [] {
        std::string s = "pgg-abi=" + std::to_string(kPggAbiVersion);
        s += ";rng=" + std::to_string(kRngAlgorithmVersion);
        s += ";noise=" + std::to_string(kNoiseAlgorithmVersion);
        s += ";cc=";
        s += compilerTag();
        s += ";isa=";
        s += isaTag();
        s += ";fp=";
        s += fpTag();
        return s;
    }();
    return kProfile;
}

uint64_t numericProfileId() {
    static const uint64_t kId = fnv1a64(numericProfileString());
    return kId;
}

}  // namespace pgg
