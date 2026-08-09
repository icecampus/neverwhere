// mask::MaskField: the node-mask silhouette extruded into a plate with a
// sloped skirt — slab defaults, the spread ramp, the sink fraction, the
// micro relief (amplitude, bilinear sampling, contour fade) and the
// end-to-end surface-nets pipeline (sample -> regularize -> extract).
#include <algorithm>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <highground_core/mask_field.h>
#include <highground_core/surface_nets.h>

namespace {

// A 2x2 node block plus a detached node (a small blob); zero border ring.
const std::uint8_t kNodes[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
};

mask::MaskField makeField(mask::MaskFieldParams params) {
    params.cellSize = 0.09f; // coarse: test speed
    return mask::MaskField(params, &kNodes[0][0], 8, 8);
}

float borderMin(const mask::MaskField& field, const std::vector<float>& samples) {
    const int px = field.sizeX() + 1;
    const int py = field.sizeY() + 1;
    const int pz = field.sizeZ() + 1;
    auto val = [&](int x, int y, int z) -> float {
        return samples[(static_cast<size_t>(y) * pz + z) * px + x];
    };
    float minVal = 1e9f;
    for (int y = 0; y < py; ++y) {
        for (int z = 0; z < pz; ++z) {
            for (int x = 0; x < px; ++x) {
                if (x == 0 || y == 0 || z == 0 || x == px - 1 || y == py - 1 || z == pz - 1) {
                    minVal = std::min(minVal, val(x, y, z));
                }
            }
        }
    }
    return minVal;
}

cliff::Mesh extractChecked(const mask::MaskField& field) {
    cliff::ScalarFieldView view = field.view();
    std::vector<float> samples;
    field.sample(samples);
    cliff::RegularizeStats reg;
    cliff::regularizeSigns(view, samples, &reg);
    EXPECT_EQ(reg.remaining, 0);
    cliff::Mesh mesh = cliff::extractSurfaceNets(view, samples, nullptr);
    EXPECT_FALSE(mesh.vertices.empty());
    EXPECT_FALSE(mesh.indices.empty());
    const cliff::WatertightReport report = cliff::checkWatertight(mesh);
    EXPECT_TRUE(report.ok()) << report.badEdges << " bad of " << report.undirectedEdges << " edges";
    return mesh;
}

} // namespace

TEST(MaskField, SlabDefaults) {
    const mask::MaskField field = makeField({});
    const float hh = 0.5f * field.params().height;
    const cliff::Mesh mesh = extractChecked(field);
    float yMin = 1e9f;
    float yMax = -1e9f;
    for (const cliff::MeshVertex& v : mesh.vertices) {
        yMin = std::min(yMin, v.py);
        yMax = std::max(yMax, v.py);
    }
    EXPECT_NEAR(yMax, hh, 2.0f * field.params().cellSize);
    EXPECT_NEAR(yMin, -hh, 2.0f * field.params().cellSize);
    // Deep inside the block, on the detached node and on the shared edge of
    // two on-nodes (no crack: the fill is C0 across cells).
    EXPECT_LT(field.eval(glm::vec3(2.5f, 0.0f, 2.5f)), 0.0f);
    EXPECT_LT(field.eval(glm::vec3(5.0f, 0.0f, 5.0f)), 0.0f);
    EXPECT_LT(field.eval(glm::vec3(2.5f, 0.0f, 2.0f)), 0.0f);
    // The iso-0.5 contour: the wall cuts an on->off edge at its midpoint.
    EXPECT_LT(field.eval(glm::vec3(3.4f, 0.0f, 2.2f)), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(3.6f, 0.0f, 2.2f)), 0.0f);
}

TEST(MaskField, SpreadSkirt) {
    // spread 0.5, height 0.2, sink 0.5: top(s) = 0.1 - 0.4*s — the skirt
    // crosses the grid plane mid-band and reaches the bottom at the foot.
    mask::MaskFieldParams params;
    params.spreadDistance = 0.5f;
    const mask::MaskField field = makeField(params);
    // The core keeps the full height right up to the wall.
    EXPECT_LT(field.eval(glm::vec3(2.5f, 0.08f, 2.5f)), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(2.5f, 0.12f, 2.5f)), 0.0f);
    // The ramp: at x = 3.6 (s = 0.1) the top is at 0.06, at x = 3.9
    // (s = 0.4) it is at -0.06.
    EXPECT_LT(field.eval(glm::vec3(3.6f, 0.04f, 2.2f)), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(3.6f, 0.08f, 2.2f)), 0.0f);
    EXPECT_LT(field.eval(glm::vec3(3.9f, -0.08f, 2.2f)), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(3.9f, -0.04f, 2.2f)), 0.0f);
    // Grid-plane crossing halfway across the band (x = 3.75, s = 0.25).
    EXPECT_LT(field.eval(glm::vec3(3.75f, -0.02f, 2.2f)), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(3.75f, 0.02f, 2.2f)), 0.0f);
    // The foot at the bottom plane; the corner diagonal rounds off.
    EXPECT_LT(field.eval(glm::vec3(3.9f, -0.09f, 2.2f)), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(4.1f, -0.09f, 2.2f)), 0.0f);
    EXPECT_LT(field.eval(glm::vec3(3.55f, -0.09f, 3.55f)), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(3.8f, -0.09f, 3.8f)), 0.0f);
    // The field border stays positive (the zero ring is wider than spread).
    std::vector<float> samples;
    field.sample(samples);
    EXPECT_GT(borderMin(field, samples), 0.0f);
    extractChecked(field);
}

TEST(MaskField, SinkFraction) {
    // height 0.2, sink 0.75: the slab spans y = -0.15..+0.05.
    mask::MaskFieldParams params;
    params.sinkFraction = 0.75f;
    const mask::MaskField field = makeField(params);
    EXPECT_LT(field.eval(glm::vec3(2.5f, 0.03f, 2.5f)), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(2.5f, 0.07f, 2.5f)), 0.0f);
    EXPECT_LT(field.eval(glm::vec3(2.5f, -0.13f, 2.5f)), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(2.5f, -0.17f, 2.5f)), 0.0f);
    extractChecked(field);

    // sink 0: the bottom sits on the grid plane, everything above it.
    mask::MaskFieldParams dry;
    dry.sinkFraction = 0.0f;
    const mask::MaskField dryField = makeField(dry);
    EXPECT_LT(dryField.eval(glm::vec3(2.5f, 0.02f, 2.5f)), 0.0f);
    EXPECT_GT(dryField.eval(glm::vec3(2.5f, -0.02f, 2.5f)), 0.0f);
    EXPECT_LT(dryField.eval(glm::vec3(2.5f, 0.18f, 2.5f)), 0.0f);
    EXPECT_GT(dryField.eval(glm::vec3(2.5f, 0.22f, 2.5f)), 0.0f);
}

TEST(MaskField, Relief) {
    const float depth = 0.05f;
    const float hh = 0.1f; // default height 0.2
    mask::ReliefMap flat1;
    flat1.w = 4;
    flat1.h = 4;
    flat1.gray.assign(16, 1.0f);

    mask::MaskFieldParams params;
    params.reliefDepth = depth;
    params.reliefMap = &flat1;
    const mask::MaskField upField = makeField(params);
    // Deep inside the block the top moves up by exactly depth.
    EXPECT_LT(upField.eval(glm::vec3(2.5f, hh + depth - 0.02f, 2.5f)), 0.0f);
    EXPECT_GT(upField.eval(glm::vec3(2.5f, hh + depth + 0.02f, 2.5f)), 0.0f);

    // An all-zero map sinks the top by depth.
    mask::ReliefMap flat0 = flat1;
    flat0.gray.assign(16, 0.0f);
    params.reliefMap = &flat0;
    const mask::MaskField downField = makeField(params);
    EXPECT_LT(downField.eval(glm::vec3(2.5f, hh - depth - 0.02f, 2.5f)), 0.0f);
    EXPECT_GT(downField.eval(glm::vec3(2.5f, hh - depth + 0.02f, 2.5f)), 0.0f);

    // Bilinear REPEAT probe: gradient map gray[i] = i/15, tiling 1.0. At
    // (1.625, 2.125): gx = 2.5 (fx 0.5), gz = 1.0 (fz 0) ->
    // sample = 0.5*(6/15 + 7/15); the contour fade (~0.93 here) is dwarfed
    // by the probe tolerance.
    mask::ReliefMap grad;
    grad.w = 4;
    grad.h = 4;
    grad.gray.resize(16);
    for (int i = 0; i < 16; ++i) {
        grad.gray[static_cast<size_t>(i)] = static_cast<float>(i) / 15.0f;
    }
    params.reliefMap = &grad;
    const mask::MaskField gradField = makeField(params);
    const float gy = hh + depth * (0.5f * (6.0f / 15.0f + 7.0f / 15.0f) - 0.5f) * 2.0f;
    EXPECT_LT(gradField.eval(glm::vec3(1.625f, gy - 0.01f, 2.125f)), 0.0f);
    EXPECT_GT(gradField.eval(glm::vec3(1.625f, gy + 0.01f, 2.125f)), 0.0f);

    // The contour fade: near the wall the all-zero map barely sinks the
    // top with the default fade, but sinks it by the full depth with
    // reliefFade = 0. At (3.45, 2.2), s = -0.05: faded top ~ 0.087, unfaded
    // top = 0.05 — a probe at y = 0.07 separates them.
    params.reliefMap = &flat0;
    const mask::MaskField faded = makeField(params);
    params.reliefFade = 0.0f;
    const mask::MaskField unfaded = makeField(params);
    EXPECT_LT(faded.eval(glm::vec3(3.45f, 0.07f, 2.2f)), 0.0f);
    EXPECT_GT(unfaded.eval(glm::vec3(3.45f, 0.07f, 2.2f)), 0.0f);

    // Watertight with the relief on: the uniform map shifts the whole iso
    // surface, the contract must hold.
    params.reliefFade = 0.15f;
    params.reliefMap = &flat1;
    const mask::MaskField reliefField = makeField(params);
    extractChecked(reliefField);
}

TEST(MaskField, ReliefMapFromImage) {
    // Bad input yields an empty map.
    EXPECT_TRUE(mask::reliefMapFromImage(nullptr, 4, 4, 1).gray.empty());

    // A 64x32 ramp low-passes to 32x16; a uniform field stays uniform.
    std::vector<std::uint8_t> ramp(64 * 32);
    for (int y = 0; y < 32; ++y) {
        for (int x = 0; x < 64; ++x) {
            ramp[static_cast<size_t>(y) * 64 + x] = static_cast<std::uint8_t>(x * 4);
        }
    }
    const mask::ReliefMap lp = mask::reliefMapFromImage(ramp.data(), 64, 32, 1);
    ASSERT_EQ(lp.w, 32);
    ASSERT_EQ(lp.h, 16);
    // The low-pass preserves the average level: mean stays ~mid-ramp.
    float mean = 0.0f;
    for (float g : lp.gray) {
        mean += g;
    }
    mean /= static_cast<float>(lp.gray.size());
    EXPECT_NEAR(mean, 126.0f / 255.0f, 0.02f);

    // Strided input (RGBA8 -> R channel).
    std::vector<std::uint8_t> rgba(8 * 8 * 4, 0);
    for (size_t i = 0; i < 64; ++i) {
        rgba[i * 4] = 255;
    }
    const mask::ReliefMap white = mask::reliefMapFromImage(rgba.data(), 8, 8, 4);
    ASSERT_EQ(white.w, 8);
    ASSERT_EQ(white.h, 8);
    for (float g : white.gray) {
        EXPECT_FLOAT_EQ(g, 1.0f);
    }
}
