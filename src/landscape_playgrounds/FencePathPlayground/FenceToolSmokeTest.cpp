#include "pch.h"

#include "FenceToolSmokeTest.h"

#include <string>

#include <spdlog/spdlog.h>

#include "FenceModel.h"

namespace {

int g_failures = 0;

void check(bool ok, const std::string& name) {
    if (ok) {
        spdlog::info("TEST PASS: {}", name);
    } else {
        spdlog::error("TEST FAIL: {}", name);
        ++g_failures;
    }
}

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

void testSegmentation() {
    FenceModel model;
    model.reset(24, 24);

    // New fences from empty ground, free end (N = cells under the stroke).
    check(planSignature(model.planStroke({2, 2}, {1, 0}, 3)) == "P|S1|P", "layout new N=3");
    check(planSignature(model.planStroke({2, 2}, {1, 0}, 4)) == "P|S2|P", "layout new N=4");
    check(planSignature(model.planStroke({2, 2}, {1, 0}, 5)) == "P|S1|P|S1|P", "layout new N=5");
    check(planSignature(model.planStroke({2, 2}, {1, 0}, 6)) == "P|S2|P|S1|P", "layout new N=6");
    check(planSignature(model.planStroke({2, 2}, {1, 0}, 7)) == "P|S2|P|S2|P", "layout new N=7");
    check(planSignature(model.planStroke({2, 2}, {1, 0}, 8)) == "P|S2|P|S1|P|S1|P", "layout new N=8");

    // Too short to hold a fence.
    check(!model.planStroke({2, 2}, {1, 0}, 1).valid, "layout new N=1 rejected");
    check(!model.planStroke({2, 2}, {1, 0}, 2).valid, "layout new N=2 rejected");

    // Extensions from an existing post (the anchor spends no cell).
    check(model.applyStroke({2, 20}, {1, 0}, 4), "apply base fence for extension tests");
    check(planSignature(model.planStroke({5, 20}, {1, 0}, 2)) == "S1|P", "layout ext N=2");
    check(planSignature(model.planStroke({5, 20}, {1, 0}, 3)) == "S2|P", "layout ext N=3");
    check(planSignature(model.planStroke({5, 20}, {1, 0}, 4)) == "S1|P|S1|P", "layout ext N=4");
    check(planSignature(model.planStroke({5, 20}, {1, 0}, 5)) == "S2|P|S1|P", "layout ext N=5");
    check(!model.planStroke({5, 20}, {1, 0}, 1).valid, "layout ext N=1 rejected");

    // A stroke cannot start on a section cell.
    check(!model.planStroke({3, 20}, {0, 1}, 3).valid, "stroke cannot start on a section");
}

void testMergeAndBlock() {
    FenceModel model;
    model.reset(24, 24);

    // Fence A: posts at (2,2) and (5,2). Fence B: posts at (13,2) and (16,2).
    check(model.applyStroke({2, 2}, {1, 0}, 4), "merge test: fence A drawn");
    check(model.applyStroke({13, 2}, {1, 0}, 4), "merge test: fence B drawn");
    check(model.fenceCount() == 2, "merge test: two independent fences");
    check(model.fenceAt({2, 2}) != model.fenceAt({13, 2}), "merge test: distinct fence ids");

    // Extend B's left post leftwards into A's right post: the run stops one
    // cell before the blocker post and the last section connects to it.
    const FenceModel::StrokePlan merge = model.planStroke({13, 2}, {-1, 0}, 8);
    check(merge.valid && merge.connectPostId >= 0, "merge test: plan sees the blocker post");
    check(planSignature(merge) == "S2|P|S2|P|S1>B", "merge test: plan layout");
    check(model.applyStroke({13, 2}, {-1, 0}, 8), "merge test: applied");
    check(model.fenceCount() == 1, "merge test: fences merged");
    check(model.fenceAt({2, 2}) == model.fenceAt({16, 2}), "merge test: one fence id");

    // A stroke ending next to another fence's SECTION stops without merging:
    // fence C runs vertically and its last cell neighbours a section of the
    // merged fence (the section at (6,2)-(7,2)).
    const int mergedFence = model.fenceAt({2, 2});
    check(model.applyStroke({6, 6}, {0, -1}, 3), "block test: fence C drawn");
    check(model.fenceCount() == 2, "block test: fence C independent");
    check(model.fenceAt({6, 4}) >= 0 && model.fenceAt({6, 4}) != mergedFence,
        "block test: end post next to a section stays a separate fence");
    check(model.pieceAt({6, 3}) == nullptr, "block test: section cell untouched");
}

void testTeeJunction() {
    FenceModel model;
    model.reset(24, 24);

    // Horizontal spine with a mid post, then a vertical branch off it.
    check(model.applyStroke({2, 10}, {1, 0}, 6), "tee: spine drawn"); // posts (2,10),(5,10),(7,10)
    check(model.applyStroke({5, 10}, {0, 1}, 2), "tee: branch drawn"); // S (5,11), P (5,12)
    check(model.fenceCount() == 1, "tee: single fence");

    const FencePiece* junction = model.pieceAt({5, 10});
    check(junction && junction->kind == FencePieceKind::Post, "tee: junction post exists");
    int degree = 0;
    for (const FencePiece& piece : model.pieces()) {
        if (piece.kind == FencePieceKind::Section &&
            (piece.postA == junction->id || piece.postB == junction->id)) {
            ++degree;
        }
    }
    check(degree == 3, "tee: junction post has 3 incident sections");
}

void testSplitAndRejoin() {
    FenceModel model;
    model.reset(24, 24);

    // P S2 P S2 P along row 8: posts (2,8), (5,8), (8,8).
    check(model.applyStroke({2, 8}, {1, 0}, 7), "split: fence drawn");
    check(model.fenceCount() == 1, "split: one fence");

    // Deleting the middle post drops its incident sections and splits the
    // fence into two isolated-post fences.
    check(model.erasePostAt({5, 8}), "split: middle post erased");
    check(model.fenceCount() == 2, "split: two fences after the cut");
    check(model.pieceAt({3, 8}) == nullptr && model.pieceAt({6, 8}) == nullptr,
        "split: incident sections pruned");
    check(model.fenceAt({2, 8}) >= 0 && model.fenceAt({8, 8}) >= 0 &&
            model.fenceAt({2, 8}) != model.fenceAt({8, 8}),
        "split: both ends survive as fences");

    // Rejoin: extend the left post rightwards into the right post.
    check(model.applyStroke({2, 8}, {1, 0}, 6), "split: rejoin stroke applied");
    check(model.fenceCount() == 1, "split: fences rejoined");
    check(model.pieceAt({5, 8}) && model.pieceAt({5, 8})->kind == FencePieceKind::Post,
        "split: middle post rebuilt by the stroke");

    // Deleting an end post just shortens the fence (post + its section go).
    check(model.erasePostAt({8, 8}), "split: end post erased");
    check(model.fenceCount() == 1, "split: still one fence");
    check(model.pieceAt({6, 8}) == nullptr && model.pieceAt({7, 8}) == nullptr,
        "split: trailing section pruned with the end post");
    check(model.pieceAt({5, 8}) && model.pieceAt({5, 8})->kind == FencePieceKind::Post,
        "split: new end post stands");
}

void testTranslateAndErase() {
    FenceModel model;
    model.reset(24, 24);

    check(model.applyStroke({2, 2}, {1, 0}, 4), "move: fence A drawn"); // P S2 P
    check(model.applyStroke({2, 6}, {1, 0}, 4), "move: fence B drawn");
    const int fenceA = model.fenceAt({2, 2});
    const int fenceB = model.fenceAt({2, 6});
    check(fenceA >= 0 && fenceB >= 0 && fenceA != fenceB, "move: two fences");

    // Free shift.
    check(model.canTranslate(fenceA, {0, 2}), "move: free shift allowed");
    check(model.translateFence(fenceA, {0, 2}), "move: free shift applied");
    check(model.pieceAt({2, 4}) && model.pieceAt({5, 4}), "move: pieces landed");
    check(model.fenceAt({2, 4}) == fenceA, "move: fence id kept");

    // Shift onto another fence is rejected and leaves the state untouched.
    check(!model.canTranslate(fenceA, {0, 2}), "move: shift onto fence B rejected");
    check(!model.translateFence(fenceA, {0, 2}), "move: rejected shift not applied");
    check(model.pieceAt({2, 4}) != nullptr, "move: state untouched after rejection");

    // Shift along the fence's own axis overlaps its own pieces — allowed.
    check(model.canTranslate(fenceA, {1, 0}), "move: self-overlap shift allowed");

    // Out of bounds is rejected.
    check(!model.canTranslate(fenceA, {-3, 0}), "move: out of bounds rejected");

    // Whole-fence deletion.
    const std::uint64_t before = model.version();
    check(model.eraseFence(fenceB), "erase: fence B deleted");
    check(!model.fenceExists(fenceB) && model.fenceExists(fenceA), "erase: only fence B gone");
    check(model.version() > before, "version bumps on edits");
}

} // namespace

bool runFenceToolSmokeTest() {
    g_failures = 0;
    testSegmentation();
    testMergeAndBlock();
    testTeeJunction();
    testSplitAndRejoin();
    testTranslateAndErase();
    if (g_failures == 0) {
        spdlog::info("TEST PASS: fence tool smoke (all checks)");
    } else {
        spdlog::error("TEST FAIL: fence tool smoke, {} check(s) failed", g_failures);
    }
    return g_failures == 0;
}
