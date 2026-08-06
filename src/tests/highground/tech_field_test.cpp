// tech::TechField: the TechnicalGrass ridge/valley tileset semantics as a
// scalar field — center-height table, fan continuity, border positivity and
// the end-to-end surface-nets pipeline (sample -> regularize -> extract).
#include <algorithm>
#include <cstdint>
#include <vector>

#include <gtest/gtest.h>

#include <highground_core/surface_nets.h>
#include <highground_core/tech_field.h>

namespace {

// All tile classes in one grid: Full/Line in the block interior, Lack cells
// around the (3,2) hole, Corner cells around the detached (4,2) node and an
// Opposite cell at (5,4) from the diagonal pair (5,4)-(6,5).
const std::uint8_t kVarietyNodes[8][8] = {
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 1, 1, 1, 0, 0, 0, 0},
    {0, 1, 1, 0, 1, 0, 0, 0},
    {0, 1, 1, 1, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 1, 0, 0},
    {0, 0, 0, 0, 0, 0, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
    {0, 0, 0, 0, 0, 0, 0, 0},
};

float borderMin(const tech::TechField& field, const std::vector<float>& samples) {
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

// Single-cell probe grid: 3x3 nodes, the probed cell sits at (0,0)..(1,1)
// with its center at (0.5, 0.5); the outer zero ring keeps the border contract.
tech::TechField probeField(const std::uint8_t (&cornerNodes)[2][2], float style) {
    std::uint8_t nodes[4][4] = {};
    for (int z = 0; z < 2; ++z) {
        for (int x = 0; x < 2; ++x) {
            nodes[z + 1][x + 1] = cornerNodes[z][x];
        }
    }
    tech::TechFieldParams params;
    params.style = style;
    return tech::TechField(params, &nodes[0][0], 4, 4);
}

float centerHeight(const std::uint8_t (&cornerNodes)[2][2], float style) {
    tech::TechField field = probeField(cornerNodes, style);
    return field.heightAt(1.5f, 1.5f) / field.params().levelHeight;
}

} // namespace

TEST(TechField, CenterHeightTableRidge) {
    // style = 0 (Ridge): Lack/Corner/Opposite centers at 0.5 (fold/peak/saddle).
    const std::uint8_t full[2][2] = {{1, 1}, {1, 1}};
    const std::uint8_t corner[2][2] = {{1, 0}, {0, 0}};
    const std::uint8_t lack[2][2] = {{0, 1}, {1, 1}};
    const std::uint8_t line[2][2] = {{1, 1}, {0, 0}};
    const std::uint8_t opposite[2][2] = {{1, 0}, {0, 1}};
    EXPECT_FLOAT_EQ(centerHeight(full, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(centerHeight(corner, 0.0f), 0.5f);
    EXPECT_FLOAT_EQ(centerHeight(lack, 0.0f), 0.5f);
    EXPECT_FLOAT_EQ(centerHeight(line, 0.0f), 0.5f);
    EXPECT_FLOAT_EQ(centerHeight(opposite, 0.0f), 0.5f);
}

TEST(TechField, CenterHeightTableValley) {
    // style = 1 (Valley): flat plateau over Lacks, flat floor under Corners,
    // crest through Opposites; Full/Line unchanged.
    const std::uint8_t full[2][2] = {{1, 1}, {1, 1}};
    const std::uint8_t corner[2][2] = {{1, 0}, {0, 0}};
    const std::uint8_t lack[2][2] = {{0, 1}, {1, 1}};
    const std::uint8_t line[2][2] = {{1, 1}, {0, 0}};
    const std::uint8_t opposite[2][2] = {{1, 0}, {0, 1}};
    EXPECT_FLOAT_EQ(centerHeight(full, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(centerHeight(corner, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(centerHeight(lack, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(centerHeight(line, 1.0f), 0.5f);
    EXPECT_FLOAT_EQ(centerHeight(opposite, 1.0f), 1.0f);
}

TEST(TechField, CornerHeightsAndFanPlane) {
    // Corner nodes keep their exact node heights; a Line cell is the planar
    // ramp from the raised edge to the lowered one in both styles.
    const std::uint8_t corner[2][2] = {{1, 0}, {0, 0}};
    tech::TechField field = probeField(corner, 0.0f);
    const float level = field.params().levelHeight;
    EXPECT_FLOAT_EQ(field.heightAt(1.0f, 1.0f), level); // the raised node itself
    EXPECT_FLOAT_EQ(field.heightAt(2.0f, 1.0f), 0.0f);
    EXPECT_FLOAT_EQ(field.heightAt(1.0f, 2.0f), 0.0f);

    const std::uint8_t line[2][2] = {{1, 1}, {0, 0}};
    tech::TechField lineField = probeField(line, 0.0f);
    // Ramp along v: h = (1 - v) at any u.
    EXPECT_NEAR(lineField.heightAt(1.25f, 1.25f) / level, 0.75f, 1e-4f);
    EXPECT_NEAR(lineField.heightAt(1.75f, 1.75f) / level, 0.25f, 1e-4f);
}

TEST(TechField, SharedEdgeContinuity) {
    // Approaching a shared cell border from either side must give the same
    // height: the fan reduces to the shared-node linear ramp on the border.
    tech::TechFieldParams params;
    params.style = 0.3f; // a blend value: centers differ per cell class
    tech::TechField field(params, &kVarietyNodes[0][0], 8, 8);
    const float level = field.params().levelHeight;
    for (float z = 1.2f; z < 3.0f; z += 0.2f) {
        EXPECT_NEAR(field.heightAt(2.0f - 1e-4f, z), field.heightAt(2.0f + 1e-4f, z), 1e-3f * level)
            << "x-border at z=" << z;
    }
    for (float x = 1.2f; x < 3.0f; x += 0.2f) {
        EXPECT_NEAR(field.heightAt(x, 2.0f - 1e-4f), field.heightAt(x, 2.0f + 1e-4f), 1e-3f * level)
            << "z-border at x=" << x;
    }
}

TEST(TechField, SoftenPreservesAnchors) {
    const std::uint8_t line[2][2] = {{1, 1}, {0, 0}};
    tech::TechFieldParams params;
    params.soften = 1.0f;
    std::uint8_t nodes[4][4] = {};
    nodes[1][1] = 1;
    nodes[1][2] = 1;
    tech::TechField field(params, &nodes[0][0], 4, 4);
    const float level = params.levelHeight;
    // 0 / 0.5 / 1 are smoothstep fixed points...
    EXPECT_FLOAT_EQ(field.heightAt(1.5f, 1.0f), level);      // raised edge midpoint
    EXPECT_FLOAT_EQ(field.heightAt(1.5f, 2.0f), 0.0f);       // lowered edge midpoint
    EXPECT_FLOAT_EQ(field.heightAt(1.5f, 1.5f), 0.5f * level); // ramp middle stays
    // ...and a plain ramp point gets the shouldered value s(0.25) = 0.15625.
    EXPECT_NEAR(field.heightAt(1.5f, 1.75f) / level, 0.15625f, 1e-4f);
}

TEST(TechField, PipelineWatertightBothStyles) {
    for (const float style : {0.0f, 1.0f}) {
        tech::TechFieldParams params;
        params.cellSize = 0.09f; // coarse: test speed
        params.style = style;
        params.creaseWidth = 0.08f;
        tech::TechField field(params, &kVarietyNodes[0][0], 8, 8);
        std::vector<float> samples;
        field.sample(samples);

        // The padding must keep the field strictly positive on the whole grid
        // border, otherwise the solid is clipped and the mesh comes out open.
        EXPECT_GT(borderMin(field, samples), 0.0f) << "style=" << style;

        cliff::ScalarFieldView view = field.view();
        cliff::RegularizeStats regStats;
        cliff::regularizeSigns(view, samples, &regStats);
        const cliff::Mesh mesh = cliff::extractSurfaceNets(view, samples, nullptr);

        EXPECT_EQ(regStats.remaining, 0) << "style=" << style;
        ASSERT_FALSE(mesh.vertices.empty()) << "style=" << style;
        EXPECT_GT(mesh.indices.size() / 3, 0u) << "style=" << style;

        const cliff::WatertightReport report = cliff::checkWatertight(mesh);
        EXPECT_TRUE(report.ok()) << "style=" << style
            << " bad edges: " << report.badEdges << " of " << report.undirectedEdges;

        float pyMax = -1e9f;
        float gMax = -1e9f;
        for (const cliff::MeshVertex& v : mesh.vertices) {
            pyMax = std::max(pyMax, v.py);
            gMax = std::max(gMax, v.groove);
        }
        // Full plateau tops sit exactly at levelHeight (one voxel of slack).
        EXPECT_NEAR(pyMax, params.levelHeight, 2.0f * params.cellSize) << "style=" << style;
        // The crease channel reaches ~1 at the raised-cell borders.
        EXPECT_GT(gMax, 0.5f) << "style=" << style << ": no outline detected";
    }
}

TEST(TechField, EmptyNodesYieldNoSurface) {
    // All-zero nodes: no bumps, no ground sheet — the field is positive
    // everywhere (an empty brush extracts no mesh).
    const std::uint8_t nodes[4][4] = {};
    tech::TechField field(tech::TechFieldParams{}, &nodes[0][0], 4, 4);
    EXPECT_FLOAT_EQ(field.heightAt(1.5f, 1.5f), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(1.5f, 0.05f, 1.5f)), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(1.5f, -0.05f, 1.5f)), 0.0f);
    EXPECT_GT(field.eval(glm::vec3(1.5f, -10.0f, 1.5f)), 0.0f);
}
