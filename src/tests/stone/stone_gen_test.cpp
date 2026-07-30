// stone_gen: stone-cube SDF -> surface-nets mesh pipeline checks.
#include <algorithm>
#include <cmath>
#include <filesystem>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <stone_gen/stone_bake.h>
#include <stone_gen/stone_field.h>
#include <stone_gen/stone_mesh.h>
#include <stone_gen/stone_sdf.h>

namespace {

TEST(StoneGen, SdfSigns) {
    const stone_gen::StoneSdf sdf(stone_gen::StoneCubeParams{});
    // The center of the box is inside the solid, far away is outside.
    EXPECT_LT(sdf.eval(glm::vec3(0.0f)), 0.0f);
    EXPECT_GT(sdf.eval(glm::vec3(10.0f)), 0.0f);
    // A surface point on the reference box face is near zero.
    const float d = sdf.eval(glm::vec3(1.0f, 0.2f, 0.0f));
    EXPECT_LT(std::fabs(d), 0.35f);
}

TEST(StoneGen, MeshWatertight) {
    const stone_gen::StoneSdf sdf(stone_gen::StoneCubeParams{});
    stone_gen::MeshParams meshParams;
    meshParams.cellSize = 0.06f; // coarser than the playground default: test speed
    const stone_gen::StoneMesh mesh = stone_gen::generateMesh(sdf, meshParams);

    EXPECT_EQ(mesh.remainingSaddles, 0);
    ASSERT_FALSE(mesh.vertices.empty());
    ASSERT_GT(mesh.indices.size() / 3, 0u);
    EXPECT_EQ(mesh.watertightBadEdges, 0);

    // Vertices stay inside the field domain (bbox + bulge + padding).
    float maxAbs = 0.0f;
    for (const stone_gen::StoneMeshVertex& v : mesh.vertices) {
        maxAbs = std::max(maxAbs, std::fabs(v.pos.x));
        EXPECT_GE(v.uv.x, -0.05f);
        EXPECT_LE(v.uv.x, 1.05f);
    }
    EXPECT_LT(maxAbs, 2.0f);
    EXPECT_GT(maxAbs, 0.8f);

    // Cell factor spans the groove range (stones and grooves both exist).
    float fMin = 2.0f;
    float fMax = -1.0f;
    for (const stone_gen::StoneMeshVertex& v : mesh.vertices) {
        fMin = std::min(fMin, v.cellFactor);
        fMax = std::max(fMax, v.cellFactor);
    }
    EXPECT_LT(fMin, 0.3f);
    EXPECT_GT(fMax, 0.7f);
}

TEST(StoneGen, MeshSeedVariation) {
    stone_gen::StoneCubeParams params;
    const stone_gen::StoneSdf sdfA(params);
    params.shape2[3] = 3.0f; // seed
    const stone_gen::StoneSdf sdfB(params);

    stone_gen::MeshParams meshParams;
    meshParams.cellSize = 0.08f;
    const stone_gen::StoneMesh meshA = stone_gen::generateMesh(sdfA, meshParams);
    const stone_gen::StoneMesh meshB = stone_gen::generateMesh(sdfB, meshParams);

    ASSERT_FALSE(meshA.vertices.empty());
    ASSERT_FALSE(meshB.vertices.empty());
    // Different seeds -> different fields -> different vertex clouds.
    float sumA = 0.0f;
    float sumB = 0.0f;
    for (const stone_gen::StoneMeshVertex& v : meshA.vertices) {
        sumA += v.pos.x + v.pos.y + v.pos.z;
    }
    for (const stone_gen::StoneMeshVertex& v : meshB.vertices) {
        sumB += v.pos.x + v.pos.y + v.pos.z;
    }
    EXPECT_NE(sumA, sumB);
}

TEST(StoneGen, FieldWatertight) {
    // Node blob (4x4 plateau + zero border) -> StoneField -> surface nets.
    const int nodesX = 6;
    const int nodesY = 6;
    std::uint8_t nodes[nodesX * nodesY] = {};
    for (int y = 1; y <= 4; ++y) {
        for (int x = 1; x <= 4; ++x) {
            nodes[y * nodesX + x] = 1;
        }
    }

    stone_gen::StoneFieldParams params;
    params.base.cellSize = 0.09f; // coarser than the UI default: test speed
    params.base.groundEnabled = false;
    stone_gen::StoneField field(params, nodes, nodesX, nodesY);

    // Signs: plateau center is inside the solid, far away is outside.
    const glm::vec3 origin = field.view().origin;
    EXPECT_LT(field.eval(glm::vec3(2.5f, 0.5f, 2.5f)), 0.0f);
    EXPECT_GT(field.eval(origin), 0.0f);

    cliff::ScalarFieldView view = field.view();
    std::vector<float> samples;
    field.sample(samples);
    cliff::RegularizeStats regStats;
    cliff::regularizeSigns(view, samples, &regStats);
    const cliff::Mesh mesh = cliff::extractSurfaceNets(view, samples, nullptr);

    EXPECT_EQ(regStats.remaining, 0);
    ASSERT_FALSE(mesh.vertices.empty());
    ASSERT_FALSE(mesh.indices.empty());
    const cliff::WatertightReport report = cliff::checkWatertight(mesh);
    EXPECT_TRUE(report.ok()) << report.badEdges << " bad of " << report.undirectedEdges << " edges";

    // Groove attribute spans the carve range (stone faces and groove floors).
    float gMin = 1e9f;
    float gMax = -1e9f;
    for (const cliff::MeshVertex& v : mesh.vertices) {
        gMin = std::min(gMin, v.groove);
        gMax = std::max(gMax, v.groove);
    }
    EXPECT_LT(gMin, params.grooveDepth * 0.3f);
    EXPECT_GT(gMax, params.grooveDepth * 0.5f);
}

TEST(StoneGen, FlatTop) {
    // Same node blob as FieldWatertight.
    const int nodesX = 6;
    const int nodesY = 6;
    std::uint8_t nodes[nodesX * nodesY] = {};
    for (int y = 1; y <= 4; ++y) {
        for (int x = 1; x <= 4; ++x) {
            nodes[y * nodesX + x] = 1;
        }
    }

    stone_gen::StoneFieldParams params;
    params.base.cellSize = 0.09f;
    params.base.groundEnabled = false;

    // Zero-crossing height of eval along the vertical line through (x, z).
    auto topCrossing = [](const stone_gen::StoneField& f, float x, float z) {
        float lo = 0.5f; // inside the slab
        float hi = 1.5f; // above the top
        for (int i = 0; i < 60; ++i) {
            const float mid = 0.5f * (lo + hi);
            if (f.eval(glm::vec3(x, mid, z)) < 0.0f) {
                lo = mid;
            } else {
                hi = mid;
            }
        }
        return 0.5f * (lo + hi);
    };
    const glm::vec2 probes[] = {
        {2.0f, 2.0f}, {2.4f, 2.8f}, {2.8f, 2.2f}, {3.2f, 3.0f}, {2.2f, 3.3f}};

    // flatTop on: every interior probe crosses at the same height — the top
    // is exactly the base slab plane, the voronoi/fbm relief stays on walls.
    {
        stone_gen::StoneField field(params, nodes, nodesX, nodesY);
        const float y0 = topCrossing(field, probes[0].x, probes[0].y);
        for (const glm::vec2& q : probes) {
            EXPECT_NEAR(topCrossing(field, q.x, q.y), y0, 1e-3f);
        }
        EXPECT_NEAR(y0, params.base.plateauHeight + params.base.edgeRadius, 0.05f);
    }
    // flatTop off: the relief reaches the top, crossings spread in height.
    {
        params.flatTop = false;
        stone_gen::StoneField field(params, nodes, nodesX, nodesY);
        float yMin = 1e9f;
        float yMax = -1e9f;
        for (const glm::vec2& q : probes) {
            const float y = topCrossing(field, q.x, q.y);
            yMin = std::min(yMin, y);
            yMax = std::max(yMax, y);
        }
        EXPECT_GT(yMax - yMin, 0.01f);
    }
}

TEST(StoneGen, RimStitch) {
    // Same node blob as FieldWatertight.
    const int nodesX = 6;
    const int nodesY = 6;
    std::uint8_t nodes[nodesX * nodesY] = {};
    for (int y = 1; y <= 4; ++y) {
        for (int x = 1; x <= 4; ++x) {
            nodes[y * nodesX + x] = 1;
        }
    }

    stone_gen::StoneFieldParams params;
    params.base.cellSize = 0.09f;
    params.base.groundEnabled = false;
    stone_gen::StoneField field(params, nodes, nodesX, nodesY);

    const float topY = params.base.plateauHeight + params.base.edgeRadius;
    const glm::vec2 center(2.5f, 2.5f);
    int bulgePts = 0;
    int scoopPts = 0;
    int interiorHoles = 0;
    float bulgeMinR = 1e9f;
    float scoopMinR = 1e9f;
    for (float x = 0.6f; x <= 4.6f; x += 0.06f) {
        for (float z = 0.6f; z <= 4.6f; z += 0.06f) {
            const float r = glm::length(glm::vec2(x, z) - center);
            // Stone bulges wrap onto the top: solid above the base top plane.
            if (field.eval(glm::vec3(x, topY + 0.02f, z)) < 0.0f) {
                ++bulgePts;
                bulgeMinR = std::min(bulgeMinR, r);
            }
            // Groove mouths scoop the top down: air pockets just under the
            // top plane, but only over a solid column.
            if (field.eval(glm::vec3(x, topY - 0.025f, z)) > 0.0f &&
                field.eval(glm::vec3(x, 0.5f, z)) < 0.0f) {
                ++scoopPts;
                scoopMinR = std::min(scoopMinR, r);
            }
            // The interior top stays solid right under the plane (no scoops).
            if (r < 0.9f && field.eval(glm::vec3(x, topY - 0.025f, z)) > 0.0f) {
                ++interiorHoles;
            }
        }
    }
    EXPECT_GT(bulgePts, 3);
    EXPECT_GT(bulgeMinR, 1.0f) << "bulge reached the top interior";
    EXPECT_GT(scoopPts, 3);
    EXPECT_GT(scoopMinR, 1.0f) << "scoop reached the top interior";
    EXPECT_EQ(interiorHoles, 0);
}

TEST(StoneGen, BakeAndExport) {
    const stone_gen::StoneSdf sdf(stone_gen::StoneCubeParams{});
    stone_gen::MeshParams meshParams;
    meshParams.cellSize = 0.08f;
    const stone_gen::StoneMesh mesh = stone_gen::generateMesh(sdf, meshParams);
    ASSERT_FALSE(mesh.vertices.empty());

    stone_gen::BakeParams bakeParams;
    bakeParams.textureSize = 128;
    bakeParams.aoTaps = 3;
    bakeParams.dilationPx = 2;
    const stone_gen::BakedTextures baked = stone_gen::bakeTextures(sdf, mesh, bakeParams);

    ASSERT_EQ(baked.size, 128);
    ASSERT_EQ(baked.albedo.size(), 128u * 128u * 4u);
    ASSERT_EQ(baked.normal.size(), 128u * 128u * 4u);

    // Coverage: a meaningful share of texels is non-black (stones + dilation).
    size_t nonZero = 0;
    for (const std::uint8_t v : baked.albedo) {
        nonZero += v != 0 ? 1 : 0;
    }
    EXPECT_GT(nonZero, baked.albedo.size() / 4u);

    // Determinism: same inputs -> identical bake.
    const stone_gen::BakedTextures baked2 = stone_gen::bakeTextures(sdf, mesh, bakeParams);
    EXPECT_EQ(baked.albedo, baked2.albedo);
    EXPECT_EQ(baked.normal, baked2.normal);

    // Export writes the three artifacts.
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "stone_gen_test_out";
    fs::create_directories(dir);
    const fs::path objPath = dir / "mesh.obj";
    const fs::path albedoPath = dir / "albedo.png";
    const fs::path normalPath = dir / "normal.png";
    EXPECT_TRUE(stone_gen::writeObj(objPath.string(), mesh));
    EXPECT_TRUE(stone_gen::writePng(albedoPath.string(), baked.size, baked.size, baked.albedo));
    EXPECT_TRUE(stone_gen::writePng(normalPath.string(), baked.size, baked.size, baked.normal));
    std::error_code ec;
    EXPECT_GT(fs::file_size(objPath, ec), 0u);
    EXPECT_GT(fs::file_size(albedoPath, ec), 0u);
    EXPECT_GT(fs::file_size(normalPath, ec), 0u);
    fs::remove_all(dir, ec);
}

} // namespace
