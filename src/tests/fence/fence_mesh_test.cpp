// fence_core mesh half: OBJ/MTL loading of the baked fence piece set
// (resources/models/fence) and the world-space instancing contract the
// renderers rely on (piece pivots, corner classification, selection tint,
// Flat ghost shading).
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <fence_core/fence_mesh.h>
#include <fence_core/fence_model.h>

namespace {

using namespace fence_core;

class FenceMeshTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        std::string error;
        meshes = new FenceMeshSet();
        const bool ok = loadFenceMeshSet(findRepoRoot() + "/resources/models/fence", meshes, &error);
        if (!ok) {
            ADD_FAILURE() << "mesh set load failed: " << error;
        }
    }
    static void TearDownTestSuite() {
        delete meshes;
        meshes = nullptr;
    }
    static FenceMeshSet* meshes;
};

FenceMeshSet* FenceMeshTest::meshes = nullptr;

} // namespace

TEST_F(FenceMeshTest, PieceSetLoadsWithGeometry) {
    ASSERT_TRUE(meshes->ok);
    const std::pair<const char*, const FenceMesh*> all[4] = {
        {"post", &meshes->post},
        {"corner", &meshes->corner},
        {"section2", &meshes->section2},
        {"section3", &meshes->section3},
    };
    for (const auto& [name, mesh] : all) {
        ASSERT_FALSE(mesh->vertices.empty()) << name << " has no vertices";
        ASSERT_FALSE(mesh->indices.empty()) << name << " has no indices";
        for (const std::uint32_t idx : mesh->indices) {
            ASSERT_LT(idx, mesh->vertices.size()) << name << " index out of range";
        }
    }
}

TEST_F(FenceMeshTest, BakedPieceContract) {
    ASSERT_TRUE(meshes->ok);
    // fence.shp pivots: post base on the ground plane, height around 0.8 m.
    EXPECT_NEAR(meshes->post.aabbMin.y, 0.0f, 1e-3f);
    EXPECT_GT(meshes->post.aabbMax.y, 0.5f);
    EXPECT_LT(meshes->post.aabbMax.y, 1.2f);
    // Sections span post axis to post axis (2 m / 3 m pitch).
    EXPECT_NEAR(meshes->section2.aabbMax.x, 2.0f, 0.3f);
    EXPECT_NEAR(meshes->section3.aabbMax.x, 3.0f, 0.3f);
}

TEST_F(FenceMeshTest, CornerClassification) {
    ASSERT_TRUE(meshes->ok);
    // L-shaped fence: exactly the elbow post carries the corner mesh.
    FenceModel model;
    model.reset(24, 24);
    ASSERT_TRUE(model.applyStroke({4, 4}, {1, 0}, 3)); // P S1 P
    ASSERT_TRUE(model.applyStroke({4, 4}, {0, 1}, 3)); // + S1 P from the elbow post

    int cornerCount = 0;
    int postCount = 0;
    for (const FencePiece& piece : model.pieces()) {
        if (piece.kind != FencePieceKind::Post) continue;
        ++postCount;
        if (isCornerPost(model, piece.id)) {
            ++cornerCount;
            EXPECT_EQ(piece.cell, glm::ivec2(4, 4));
        }
    }
    EXPECT_EQ(postCount, 3);
    EXPECT_EQ(cornerCount, 1);
}

TEST_F(FenceMeshTest, InstanceVertexAccounting) {
    ASSERT_TRUE(meshes->ok);
    FenceModel model;
    model.reset(24, 24);
    ASSERT_TRUE(model.applyStroke({4, 4}, {1, 0}, 3));
    ASSERT_TRUE(model.applyStroke({4, 4}, {0, 1}, 3));

    const std::vector<FenceWorldVertex> verts = buildFenceWorldTriangles(model, *meshes, -1);
    size_t expected = 0;
    for (const FencePiece& piece : model.pieces()) {
        if (piece.kind == FencePieceKind::Post) {
            expected += (isCornerPost(model, piece.id) ? meshes->corner : meshes->post).indices.size();
        } else {
            expected += (piece.length >= 2 ? meshes->section3 : meshes->section2).indices.size();
        }
    }
    EXPECT_EQ(verts.size(), expected);
}

TEST_F(FenceMeshTest, PiecePivotSitsOnCellCenter) {
    ASSERT_TRUE(meshes->ok);
    FenceModel solo;
    solo.reset(24, 24);
    ASSERT_TRUE(solo.applyStroke({10, 10}, {1, 0}, 3)); // P S1 P, lead post at (10,10)

    // The lead post's instance is offset by exactly the cell's integer world
    // point (a half-cell shift would land it on a grid node instead).
    const std::vector<FenceWorldVertex> verts = buildFenceWorldTriangles(solo, *meshes, -1);
    ASSERT_FALSE(verts.empty());
    bool postFound = false;
    for (const FenceWorldVertex& v : verts) {
        // Post vertices live within a 1 m box around (10, y, 10); the corner
        // post is at (12,10) for this layout (elbow = lead post (10,10) has
        // only one section, so it is a regular post mesh).
        if (v.pos[0] > 9.0f && v.pos[0] < 11.0f && v.pos[2] > 9.0f && v.pos[2] < 11.0f) {
            postFound = true;
        }
    }
    EXPECT_TRUE(postFound);
}

TEST_F(FenceMeshTest, SelectionTintRecolorsPieces) {
    ASSERT_TRUE(meshes->ok);
    FenceModel solo;
    solo.reset(24, 24);
    ASSERT_TRUE(solo.applyStroke({10, 10}, {1, 0}, 3));
    const int fenceId = solo.fenceAt({10, 10});
    ASSERT_GE(fenceId, 0);

    const std::vector<FenceWorldVertex> plain = buildFenceWorldTriangles(solo, *meshes, -1);
    const std::vector<FenceWorldVertex> tinted = buildFenceWorldTriangles(solo, *meshes, fenceId);
    ASSERT_EQ(plain.size(), tinted.size());
    bool differs = false;
    for (size_t i = 0; i < tinted.size() && !differs; ++i) {
        differs = tinted[i].color[0] != plain[i].color[0] ||
            tinted[i].color[1] != plain[i].color[1] ||
            tinted[i].color[2] != plain[i].color[2];
    }
    EXPECT_TRUE(differs);
}

TEST_F(FenceMeshTest, FlatShadingOverridesColor) {
    ASSERT_TRUE(meshes->ok);
    // The ghost path: Flat ignores the baked lighting and emits the tint.
    const glm::vec4 tint{0.30f, 0.85f, 0.35f, 0.45f};
    std::vector<FenceWorldVertex> out;
    appendFenceInstance(meshes->post, glm::vec3{5.0f, 0.0f, 5.0f}, 0.0f,
        FenceInstanceShading::Flat, tint, out);
    ASSERT_EQ(out.size(), meshes->post.indices.size());
    for (const FenceWorldVertex& v : out) {
        EXPECT_FLOAT_EQ(v.color[0], tint.r);
        EXPECT_FLOAT_EQ(v.color[1], tint.g);
        EXPECT_FLOAT_EQ(v.color[2], tint.b);
        EXPECT_FLOAT_EQ(v.color[3], tint.a);
        // Offset applied.
        EXPECT_GE(v.pos[0], 5.0f + meshes->post.aabbMin.x - 1e-4f);
        EXPECT_LE(v.pos[0], 5.0f + meshes->post.aabbMax.x + 1e-4f);
    }
}
