// stone_gen: stone-cube SDF -> surface-nets mesh pipeline checks.
#include <algorithm>
#include <cmath>
#include <filesystem>

#include <gtest/gtest.h>

#include <glm/glm.hpp>

#include <stone_gen/stone_bake.h>
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
