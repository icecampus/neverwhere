// E3 criterion 2 (spec §15, N1/N7): within one numeric profile the result
// does not depend on the thread count. A heavy synthetic graph exercises
// every parallelized loop over the pool (>= 4096 elements): the evalField
// element loop (fbm displace on a 40k-point grid), compute_normals,
// distribute_points (candidate generation, thinning, poisson acceptance),
// realize (per-anchor fill) and distance_to (per-query). Geometry content
// hashes must be strictly equal at threads = 1/2/8.
#include <gtest/gtest.h>

#include "pgg/eval.h"
#include "pgg/src/eval/parallel.h"
#include "test_utils.h"

namespace {

// 40k+ points (res 200 -> 201^2 = 40401): fbm displace -> normals -> poisson
// scatter -> realize; plus an 8000-anchor realize and a distance_to field.
const char* kHeavy =
    "param seed: int = 42\n"
    "root_rng = rng_from_seed(seed)\n"
    "noise_rng = split_rng(root_rng, key = \"n\")\n"
    "scatter_rng = split_rng(root_rng, key = \"s\")\n"
    "b = grid(size = (20, 20), res = 200)\n"
    "n = fbm(scale = 0.35, octaves = 5, rng = noise_rng)\n"
    "disp = set_position(b, offset = @N * n * 0.5)\n"
    "rock = compute_normals(disp)\n"
    "pts = distribute_points(rock, density = 2.0, min_dist = 0.25, rng = scatter_rng)\n"
    "va = ico_sphere(subdiv = 1, radius = 0.1)\n"
    "real = realize(instance_on_points(pts, va))\n"
    "line = mesh_line(count = 8000, length = 100.0)\n"
    "sbox = box(size = (1, 1, 1))\n"
    "real2 = realize(instance_on_points(line, sbox))\n"
    "probe = grid(size = (4, 4), res = 80)\n"
    "target = ico_sphere(subdiv = 2, radius = 1.0)\n"
    "dt = set(probe, \"d\", distance_to(target))\n"
    "davg = avg_of(@d, on = dt)\n"
    "output rock\n"
    "output pts\n"
    "output real\n"
    "output real2\n"
    "output davg\n";

struct HeavyRun {
    pgg::RunResult r;
    uint64_t rock = 0, pts = 0, real = 0, real2 = 0;
    float davg = 0.0f;
};

HeavyRun runHeavy(unsigned threads) {
    HeavyRun out;
    pgg::RunParams p;
    p.threads = threads;
    out.r = pgg::run(std::string(kHeavy), p);
    pggtest::expectNoErrors(out.r);
    out.rock = pggtest::geoContentHash(pggtest::geoOutput(out.r, "rock"));
    out.pts = pggtest::geoContentHash(pggtest::geoOutput(out.r, "pts"));
    out.real = pggtest::geoContentHash(pggtest::geoOutput(out.r, "real"));
    out.real2 = pggtest::geoContentHash(pggtest::geoOutput(out.r, "real2"));
    out.davg = pgg::asF32(*pggtest::outputOf(out.r, "davg"));
    return out;
}

TEST(Parallel, HeavyGraphIsThreadCountInvariant) {
    HeavyRun h1 = runHeavy(1);
    HeavyRun h2 = runHeavy(2);
    HeavyRun h8 = runHeavy(8);
    EXPECT_EQ(h1.r.stats.threadsUsed, 1u);
    EXPECT_EQ(h2.r.stats.threadsUsed, 2u);
    EXPECT_EQ(h8.r.stats.threadsUsed, 8u);
    // The synthetic really is heavy enough to take the pool path.
    EXPECT_EQ(pggtest::geoOutput(h1.r, "rock")->pointCount(), 40401u);
    EXPECT_GT(pggtest::geoOutput(h1.r, "pts")->pointCount(), 0u);
    EXPECT_EQ(pggtest::geoOutput(h1.r, "real2")->pointCount(), 64000u);
    // Strict bit equality of the content hash (one numeric profile).
    EXPECT_EQ(h1.rock, h2.rock);
    EXPECT_EQ(h1.rock, h8.rock);
    EXPECT_EQ(h1.pts, h2.pts);
    EXPECT_EQ(h1.pts, h8.pts);
    EXPECT_EQ(h1.real, h2.real);
    EXPECT_EQ(h1.real, h8.real);
    EXPECT_EQ(h1.real2, h2.real2);
    EXPECT_EQ(h1.real2, h8.real2);
    // distance_to acceptance: the reduced statistic is bit-identical too.
    EXPECT_EQ(h1.davg, h2.davg);
    EXPECT_EQ(h1.davg, h8.davg);
}

TEST(Parallel, ParallelForFillsDisjointSlotsDeterministically) {
    // Direct pool unit: same fill at 1/2/8 lanes, byte-equal buffers.
    constexpr size_t n = 100000;
    auto fill = [](unsigned threads, std::vector<uint64_t>& out) {
        pgg::parallelFor(out.size(), threads, [&](size_t s, size_t e) {
            for (size_t i = s; i < e; ++i) out[i] = i * 2654435761u + (i >> 3);
        });
    };
    std::vector<uint64_t> a(n), b(n), c(n);
    fill(1, a);
    fill(2, b);
    fill(8, c);
    EXPECT_EQ(a, b);
    EXPECT_EQ(a, c);
    // Really wrote everything.
    EXPECT_EQ(a.back(), (n - 1) * 2654435761u + ((n - 1) >> 3));
}

TEST(Parallel, NestedParallelForRunsInlineWithoutDeadlock) {
    // The anti-nesting guard: a parallelFor on a pool worker executes inline,
    // so nesting is safe and correct (the engine never nests; keep it true).
    constexpr size_t outer = 5000;   // >= threshold -> pool path for the outer loop
    constexpr size_t inner = 4096;   // == threshold -> would re-enter the pool unguarded
    std::vector<size_t> sums(outer, 0);
    pgg::parallelFor(outer, 8, [&](size_t s, size_t e) {
        for (size_t i = s; i < e; ++i) {
            size_t local = 0;  // only race-free if the inner loop stays inline
            pgg::parallelFor(inner, 8, [&](size_t s2, size_t e2) {
                for (size_t j = s2; j < e2; ++j) local += j;
            });
            sums[i] = local;
        }
    });
    for (size_t i = 0; i < outer; ++i) EXPECT_EQ(sums[i], inner * (inner - 1) / 2) << i;
}

}  // namespace
