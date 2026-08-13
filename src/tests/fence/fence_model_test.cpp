// fence_core::FenceModel — the piece-graph fence layout model shared by the
// FencePathPlayground and the editor (FenceLandscape authoring + frame
// building). Ports the playground smoke scenarios (stroke segmentation,
// merge/block, tee junction, split/rejoin, translate/erase) and guards the
// loadPieces() determinism the editor relies on: the same flat piece list
// must always derive the same piece/fence ids.
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <fence_core/fence_model.h>

namespace {

using fence_core::FenceModel;
using fence_core::FencePiece;
using fence_core::FencePieceData;
using fence_core::FencePieceKind;

// "P|S2|P|S1|P"-style signature of a stroke plan (piece kinds + lengths).
std::string planSignature(const FenceModel::StrokePlan& plan) {
    if (!plan.valid) {
        return "<invalid>";
    }
    std::string out;
    for (const FenceModel::StrokePiece& piece : plan.pieces) {
        if (!out.empty()) {
            out += '|';
        }
        if (piece.kind == FencePieceKind::Post) {
            out += 'P';
        } else {
            out += 'S';
            out += static_cast<char>('0' + piece.length);
        }
    }
    if (plan.connectPostId >= 0) {
        out += ">B";
    }
    return out;
}

} // namespace

TEST(FenceModel, StrokeSegmentation) {
    FenceModel model;
    model.reset(24, 24);

    // New fences from empty ground, free end (N = cells under the stroke).
    EXPECT_EQ(planSignature(model.planStroke({2, 2}, {1, 0}, 3)), "P|S1|P");
    EXPECT_EQ(planSignature(model.planStroke({2, 2}, {1, 0}, 4)), "P|S2|P");
    EXPECT_EQ(planSignature(model.planStroke({2, 2}, {1, 0}, 5)), "P|S1|P|S1|P");
    EXPECT_EQ(planSignature(model.planStroke({2, 2}, {1, 0}, 6)), "P|S2|P|S1|P");
    EXPECT_EQ(planSignature(model.planStroke({2, 2}, {1, 0}, 7)), "P|S2|P|S2|P");
    EXPECT_EQ(planSignature(model.planStroke({2, 2}, {1, 0}, 8)), "P|S2|P|S1|P|S1|P");

    // Too short to hold a fence.
    EXPECT_FALSE(model.planStroke({2, 2}, {1, 0}, 1).valid);
    EXPECT_FALSE(model.planStroke({2, 2}, {1, 0}, 2).valid);

    // Extensions from an existing post (the anchor spends no cell).
    ASSERT_TRUE(model.applyStroke({2, 20}, {1, 0}, 4));
    EXPECT_EQ(planSignature(model.planStroke({5, 20}, {1, 0}, 2)), "S1|P");
    EXPECT_EQ(planSignature(model.planStroke({5, 20}, {1, 0}, 3)), "S2|P");
    EXPECT_EQ(planSignature(model.planStroke({5, 20}, {1, 0}, 4)), "S1|P|S1|P");
    EXPECT_EQ(planSignature(model.planStroke({5, 20}, {1, 0}, 5)), "S2|P|S1|P");
    EXPECT_FALSE(model.planStroke({5, 20}, {1, 0}, 1).valid);

    // A stroke cannot start on a section cell.
    EXPECT_FALSE(model.planStroke({3, 20}, {0, 1}, 3).valid);
}

TEST(FenceModel, MergeAndBlock) {
    FenceModel model;
    model.reset(24, 24);

    ASSERT_TRUE(model.applyStroke({2, 2}, {1, 0}, 4));
    ASSERT_TRUE(model.applyStroke({13, 2}, {1, 0}, 4));
    ASSERT_EQ(model.fenceCount(), 2);
    EXPECT_NE(model.fenceAt({2, 2}), model.fenceAt({13, 2}));

    // Extend B's left post leftwards into A's right post: the run stops one
    // cell before the blocker post and the last section connects to it.
    const FenceModel::StrokePlan merge = model.planStroke({13, 2}, {-1, 0}, 8);
    ASSERT_TRUE(merge.valid);
    EXPECT_GE(merge.connectPostId, 0);
    EXPECT_EQ(planSignature(merge), "S2|P|S2|P|S1>B");
    ASSERT_TRUE(model.applyStroke({13, 2}, {-1, 0}, 8));
    EXPECT_EQ(model.fenceCount(), 1);
    EXPECT_EQ(model.fenceAt({2, 2}), model.fenceAt({16, 2}));

    // A stroke ending next to another fence's SECTION stops without merging.
    const int mergedFence = model.fenceAt({2, 2});
    ASSERT_TRUE(model.applyStroke({6, 6}, {0, -1}, 3));
    EXPECT_EQ(model.fenceCount(), 2);
    EXPECT_GE(model.fenceAt({6, 4}), 0);
    EXPECT_NE(model.fenceAt({6, 4}), mergedFence);
    EXPECT_EQ(model.pieceAt({6, 3}), nullptr);
}

TEST(FenceModel, TeeJunction) {
    FenceModel model;
    model.reset(24, 24);

    ASSERT_TRUE(model.applyStroke({2, 10}, {1, 0}, 6)); // posts (2,10),(5,10),(7,10)
    ASSERT_TRUE(model.applyStroke({5, 10}, {0, 1}, 2)); // S (5,11), P (5,12)
    EXPECT_EQ(model.fenceCount(), 1);

    const FencePiece* junction = model.pieceAt({5, 10});
    ASSERT_NE(junction, nullptr);
    ASSERT_EQ(junction->kind, FencePieceKind::Post);
    int degree = 0;
    for (const FencePiece& piece : model.pieces()) {
        if (piece.kind == FencePieceKind::Section &&
            (piece.postA == junction->id || piece.postB == junction->id)) {
            ++degree;
        }
    }
    EXPECT_EQ(degree, 3);
}

TEST(FenceModel, SplitAndRejoin) {
    FenceModel model;
    model.reset(24, 24);

    ASSERT_TRUE(model.applyStroke({2, 8}, {1, 0}, 7)); // posts (2,8),(5,8),(8,8)
    ASSERT_EQ(model.fenceCount(), 1);

    // Deleting the middle post drops its incident sections and splits the
    // fence into two isolated-post fences.
    ASSERT_TRUE(model.erasePostAt({5, 8}));
    EXPECT_EQ(model.fenceCount(), 2);
    EXPECT_EQ(model.pieceAt({3, 8}), nullptr);
    EXPECT_EQ(model.pieceAt({6, 8}), nullptr);
    EXPECT_NE(model.fenceAt({2, 8}), model.fenceAt({8, 8}));

    // Rejoin: extend the left post rightwards into the right post.
    ASSERT_TRUE(model.applyStroke({2, 8}, {1, 0}, 6));
    EXPECT_EQ(model.fenceCount(), 1);
    ASSERT_NE(model.pieceAt({5, 8}), nullptr);
    EXPECT_EQ(model.pieceAt({5, 8})->kind, FencePieceKind::Post);

    // Deleting an end post just shortens the fence.
    ASSERT_TRUE(model.erasePostAt({8, 8}));
    EXPECT_EQ(model.fenceCount(), 1);
    EXPECT_EQ(model.pieceAt({6, 8}), nullptr);
    EXPECT_EQ(model.pieceAt({7, 8}), nullptr);
    ASSERT_NE(model.pieceAt({5, 8}), nullptr);
    EXPECT_EQ(model.pieceAt({5, 8})->kind, FencePieceKind::Post);
}

TEST(FenceModel, TranslateAndErase) {
    FenceModel model;
    model.reset(24, 24);

    ASSERT_TRUE(model.applyStroke({2, 2}, {1, 0}, 4)); // P S2 P
    ASSERT_TRUE(model.applyStroke({2, 6}, {1, 0}, 4));
    const int fenceA = model.fenceAt({2, 2});
    const int fenceB = model.fenceAt({2, 6});
    ASSERT_GE(fenceA, 0);
    ASSERT_GE(fenceB, 0);
    ASSERT_NE(fenceA, fenceB);

    // Free shift.
    ASSERT_TRUE(model.canTranslate(fenceA, {0, 2}));
    ASSERT_TRUE(model.translateFence(fenceA, {0, 2}));
    EXPECT_NE(model.pieceAt({2, 4}), nullptr);
    EXPECT_NE(model.pieceAt({5, 4}), nullptr);
    EXPECT_EQ(model.fenceAt({2, 4}), fenceA);

    // Shift onto another fence is rejected and leaves the state untouched.
    EXPECT_FALSE(model.canTranslate(fenceA, {0, 2}));
    EXPECT_FALSE(model.translateFence(fenceA, {0, 2}));
    EXPECT_NE(model.pieceAt({2, 4}), nullptr);

    // Shift along the fence's own axis overlaps its own pieces — allowed.
    EXPECT_TRUE(model.canTranslate(fenceA, {1, 0}));

    // Out of bounds is rejected (bounded mode).
    EXPECT_FALSE(model.canTranslate(fenceA, {-3, 0}));

    // Whole-fence deletion + version bump.
    const std::uint64_t before = model.version();
    ASSERT_TRUE(model.eraseFence(fenceB));
    EXPECT_FALSE(model.fenceExists(fenceB));
    EXPECT_TRUE(model.fenceExists(fenceA));
    EXPECT_GT(model.version(), before);
}

TEST(FenceModel, UnboundedModeAllowsNegativeCells) {
    FenceModel model;
    model.reset(); // unbounded (editor maps are unbounded)

    ASSERT_TRUE(model.applyStroke({-10, -10}, {1, 0}, 5));
    EXPECT_NE(model.pieceAt({-10, -10}), nullptr);
    EXPECT_NE(model.pieceAt({-6, -10}), nullptr);
    const int fence = model.fenceAt({-10, -10});
    ASSERT_GE(fence, 0);
    // No map edge to reject a shift in unbounded mode.
    EXPECT_TRUE(model.canTranslate(fence, {-100, 100}));
}

TEST(FenceModel, LoadPiecesDerivesLinksAndIdsDeterministically) {
    // Build a reference layout interactively (L shape + independent fence).
    FenceModel reference;
    reference.reset(24, 24);
    ASSERT_TRUE(reference.applyStroke({2, 2}, {1, 0}, 7));  // posts (2,2),(5,2),(8,2)
    ASSERT_TRUE(reference.applyStroke({5, 2}, {0, 1}, 4));  // branch from the mid post
    ASSERT_TRUE(reference.applyStroke({15, 15}, {1, 0}, 3));

    // Flatten to the persisted form and rebuild twice from it.
    std::vector<FencePieceData> flat;
    for (const FencePiece& p : reference.pieces()) {
        flat.push_back({p.kind, p.cell, p.axis, p.length});
    }
    FenceModel a;
    a.reset();
    a.loadPieces(flat);
    FenceModel b;
    b.reset();
    b.loadPieces(flat);

    ASSERT_EQ(a.pieces().size(), reference.pieces().size());
    ASSERT_EQ(b.pieces().size(), reference.pieces().size());

    // Same fence ids everywhere (both rebuilds and the reference agree).
    for (const FencePiece& p : reference.pieces()) {
        EXPECT_EQ(a.fenceAt(p.cell), p.fenceId) << "fenceId mismatch at piece " << p.id;
        EXPECT_EQ(b.fenceAt(p.cell), p.fenceId) << "fenceId mismatch at piece " << p.id;
    }

    // Endpoint links derived geometrically: every section's posts sit one
    // cell before the anchor and one cell past the run.
    for (const FencePiece& p : a.pieces()) {
        if (p.kind != FencePieceKind::Section) continue;
        const FencePiece* postA = a.pieceById(p.postA);
        const FencePiece* postB = a.pieceById(p.postB);
        ASSERT_NE(postA, nullptr);
        ASSERT_NE(postB, nullptr);
        EXPECT_EQ(postA->cell, p.cell - p.axis);
        EXPECT_EQ(postB->cell, p.cell + p.axis * p.length);
        EXPECT_EQ(postA->kind, FencePieceKind::Post);
        EXPECT_EQ(postB->kind, FencePieceKind::Post);
    }

    // Component structure preserved: 2 fences, the tee junction intact.
    EXPECT_EQ(a.fenceCount(), 2);
    EXPECT_EQ(a.fenceAt({2, 2}), a.fenceAt({5, 5}));
    EXPECT_NE(a.fenceAt({2, 2}), a.fenceAt({15, 15}));
}
