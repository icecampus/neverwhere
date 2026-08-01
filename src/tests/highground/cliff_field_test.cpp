// cliff::CliffField + surface nets: end-to-end checks of the scalar-field
// cliff pipeline (injected node grid -> sample -> regularize -> extract).
// The scenario mirrors CliffFieldPlayground's runTestScenario.
#include <algorithm>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <highground_core/cliff_field.h>
#include <highground_core/surface_nets.h>

namespace {

// 7x7 demo shape from CliffFieldPlayground (bay notch, NE lobe, peninsula).
const std::uint8_t kHeightNodes[7][7] = {
    {0, 0, 0, 0, 0, 0, 0},
    {0, 0, 1, 1, 1, 0, 0},
    {0, 1, 1, 1, 1, 1, 0},
    {0, 1, 1, 0, 1, 1, 0},
    {0, 1, 1, 1, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0},
};

float borderMin(const cliff::CliffField& field, const std::vector<float>& samples) {
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

struct PipelineResult {
    cliff::Mesh mesh;
    cliff::RegularizeStats regStats;
    float border = 0.0f;
};

PipelineResult runPipeline(const std::uint8_t* nodes, int nodesX, int nodesY,
    const cliff::FieldParams& params = {}) {
    cliff::CliffField field(params, nodes, nodesX, nodesY);
    std::vector<float> samples;
    field.sample(samples);

    PipelineResult result;
    result.border = borderMin(field, samples);
    cliff::regularizeSigns(field, samples, &result.regStats);
    result.mesh = cliff::extractSurfaceNets(field, samples, nullptr);
    return result;
}

} // namespace

TEST(CliffField, DemoShapePipeline) {
    const PipelineResult result = runPipeline(&kHeightNodes[0][0], 7, 7);

    // The padding must keep the field strictly positive on the whole grid
    // border, otherwise the solid is clipped and the mesh comes out open.
    EXPECT_GT(result.border, 0.0f);
    EXPECT_EQ(result.regStats.remaining, 0);
    ASSERT_FALSE(result.mesh.vertices.empty());
    EXPECT_GT(result.mesh.indices.size() / 3, 0u);

    const cliff::WatertightReport report = cliff::checkWatertight(result.mesh);
    EXPECT_TRUE(report.ok()) << "bad edges: " << report.badEdges
        << " of " << report.undirectedEdges;

    float gMax = -1e9f;
    for (const cliff::MeshVertex& v : result.mesh.vertices) {
        gMax = std::max(gMax, v.groove);
    }
    EXPECT_GT(gMax, 0.02f) << "no groove carve detected";
}

TEST(CliffField, RectangularRegion) {
    // The demo shape embedded into a wider (9x7) node grid: the injected grid
    // may be rectangular (a brush bbox in SDFGeneratedLandscape), and the wider
    // zero margin must not change the extraction. (A node pattern that yields
    // stubborn saddle storms can still leave regStats.remaining > 0 — the
    // regularization heuristic is iterative and not guaranteed to converge on
    // every field; the reference demo shape is the convergence benchmark.)
    const std::uint8_t nodes[7][9] = {
        {0, 0, 0, 0, 0, 0, 0, 0, 0},
        {0, 0, 1, 1, 1, 0, 0, 0, 0},
        {0, 1, 1, 1, 1, 1, 0, 0, 0},
        {0, 1, 1, 0, 1, 1, 0, 0, 0},
        {0, 1, 1, 1, 0, 0, 0, 0, 0},
        {0, 0, 1, 1, 0, 0, 0, 0, 0},
        {0, 0, 0, 0, 0, 0, 0, 0, 0},
    };
    const PipelineResult result = runPipeline(&nodes[0][0], 9, 7);

    EXPECT_GT(result.border, 0.0f);
    EXPECT_EQ(result.regStats.remaining, 0);
    ASSERT_FALSE(result.mesh.vertices.empty());

    const cliff::WatertightReport report = cliff::checkWatertight(result.mesh);
    EXPECT_TRUE(report.ok()) << "bad edges: " << report.badEdges
        << " of " << report.undirectedEdges;
}

TEST(CliffField, CoarseBlockShape) {
    // SDFGeneratedLandscape smoke scenario: a solid 4x4 block of on-nodes in a
    // 6x6 grid (1-cell zero margin), sampled coarse (cellSize 0.09) for speed.
    // Regression: two adjacent saddle faces used to flip-flop their shared
    // corner on every pass, leaving regStats.remaining = 2 at any pass budget.
    const std::uint8_t nodes[6][6] = {
        {0, 0, 0, 0, 0, 0},
        {0, 1, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 0},
        {0, 1, 1, 1, 1, 0},
        {0, 0, 0, 0, 0, 0},
    };
    cliff::FieldParams params;
    params.cellSize = 0.09f;
    const PipelineResult result = runPipeline(&nodes[0][0], 6, 6, params);

    EXPECT_GT(result.border, 0.0f);
    EXPECT_EQ(result.regStats.remaining, 0);
    ASSERT_FALSE(result.mesh.vertices.empty());

    const cliff::WatertightReport report = cliff::checkWatertight(result.mesh);
    EXPECT_TRUE(report.ok()) << "bad edges: " << report.badEdges
        << " of " << report.undirectedEdges;

    float gMax = -1e9f;
    for (const cliff::MeshVertex& v : result.mesh.vertices) {
        gMax = std::max(gMax, v.groove);
    }
    EXPECT_GT(gMax, 0.02f) << "no groove carve detected";
}

TEST(CliffField, EmptyNodeGrid) {
    // All nodes off -> no solid above the ground chunk, but the pipeline
    // must stay sane (the ground slab alone is still a closed solid).
    const std::uint8_t nodes[5][5] = {};
    const PipelineResult result = runPipeline(&nodes[0][0], 5, 5);

    EXPECT_GT(result.border, 0.0f);
    const cliff::WatertightReport report = cliff::checkWatertight(result.mesh);
    EXPECT_TRUE(report.ok()) << "bad edges: " << report.badEdges
        << " of " << report.undirectedEdges;
}

TEST(CliffField, NoGroundSlab) {
    // groundEnabled = false: the raised slab stands alone — the mesh must
    // stay watertight and nothing may reach below the slab underside
    // (y = -edgeRadius; the ground chunk would extend to -groundDepth).
    cliff::FieldParams params;
    params.groundEnabled = false;
    const PipelineResult result = runPipeline(&kHeightNodes[0][0], 7, 7, params);

    EXPECT_GT(result.border, 0.0f);
    EXPECT_EQ(result.regStats.remaining, 0);
    ASSERT_FALSE(result.mesh.vertices.empty());

    const cliff::WatertightReport report = cliff::checkWatertight(result.mesh);
    EXPECT_TRUE(report.ok()) << "bad edges: " << report.badEdges
        << " of " << report.undirectedEdges;

    float minY = 1e9f;
    for (const cliff::MeshVertex& v : result.mesh.vertices) {
        minY = std::min(minY, v.py);
    }
    EXPECT_GT(minY, -0.1f) << "geometry below the slab underside";
    EXPECT_LT(minY, 0.0f);
}
