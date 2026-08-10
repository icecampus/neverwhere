#include "pch.h"

#include "FenceToolSmokeTest.h"

#include <limits>
#include <string>

#include <spdlog/spdlog.h>

#include "FenceMesh.h"
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

namespace {

void testMeshLoading(FenceMeshSet* meshes) {
    std::string error;
    check(loadFenceMeshSet(findRepoRoot() + "/resources/models/fence", meshes, &error),
        "mesh: piece set loads");
    if (!meshes->ok) {
        spdlog::error("TEST FAIL: mesh: load error: {}", error);
        return;
    }
    const std::pair<const char*, const FenceMesh*> all[4] = {
        {"post", &meshes->post},
        {"corner", &meshes->corner},
        {"section2", &meshes->section2},
        {"section3", &meshes->section3},
    };
    for (const auto& [name, mesh] : all) {
        check(!mesh->vertices.empty() && !mesh->indices.empty(),
            std::string("mesh: ") + name + " has geometry");
        bool indicesOk = true;
        for (const std::uint32_t idx : mesh->indices) {
            indicesOk = indicesOk && idx < mesh->vertices.size();
        }
        check(indicesOk, std::string("mesh: ") + name + " indices in range");
    }
    // Piece contract of fence.shp: post base on the ground plane, section
    // spans post axis to post axis.
    check(std::abs(meshes->post.aabbMin.y) < 1e-3f, "mesh: post base at y=0");
    check(meshes->post.aabbMax.y > 0.5f && meshes->post.aabbMax.y < 1.2f,
        "mesh: post height around 0.8 m");
    check(std::abs(meshes->section2.aabbMax.x - 2.0f) < 0.3f,
        "mesh: 1-cell section spans 2 m");
    check(std::abs(meshes->section3.aabbMax.x - 3.0f) < 0.3f,
        "mesh: 2-cell section spans 3 m");
}

void testMeshInstancing(const FenceMeshSet& meshes) {
    if (!meshes.ok) {
        return;
    }
    const topology_core::DiamondIsometry iso;

    // L-shaped fence: one post with two perpendicular sections -> corner.
    FenceModel model;
    model.reset(24, 24);
    model.applyStroke({4, 4}, {1, 0}, 3); // P S1 P
    model.applyStroke({4, 4}, {0, 1}, 3); // + S1 P from the elbow post
    int cornerCount = 0;
    int postCount = 0;
    for (const FencePiece& piece : model.pieces()) {
        if (piece.kind != FencePieceKind::Post) {
            continue;
        }
        ++postCount;
        if (isCornerPost(model, piece.id)) {
            ++cornerCount;
        }
    }
    check(postCount == 3, "mesh: L fence has 3 posts");
    check(cornerCount == 1, "mesh: exactly the elbow post is a corner");

    // Vertex accounting: total = sum of the per-piece mesh index counts.
    const std::vector<FenceFieldVertex> verts =
        buildFenceFieldTriangles(iso, model, meshes, -1);
    size_t expected = 0;
    for (const FencePiece& piece : model.pieces()) {
        if (piece.kind == FencePieceKind::Post) {
            expected += (isCornerPost(model, piece.id) ? meshes.corner : meshes.post).indices.size();
        } else {
            expected += (piece.length >= 2 ? meshes.section3 : meshes.section2).indices.size();
        }
    }
    check(verts.size() == expected, "mesh: instance vertex accounting");

    // Projection: the post apex lifts above its ground row (smaller screen y),
    // and every z stays inside the (0,1) depth range.
    FenceModel solo;
    solo.reset(24, 24);
    solo.applyStroke({10, 10}, {1, 0}, 3); // P S P
    const std::vector<FenceFieldVertex> soloVerts =
        buildFenceFieldTriangles(iso, solo, meshes, -1);
    const float groundY = iso.mapToField({10, 10}).y;
    float minY = std::numeric_limits<float>::max();
    float minZ = 1.0f;
    float maxZ = 0.0f;
    for (const FenceFieldVertex& v : soloVerts) {
        minY = std::min(minY, v.y);
        minZ = std::min(minZ, v.z);
        maxZ = std::max(maxZ, v.z);
    }
    check(minY < groundY - 20.0f, "mesh: post apex lifted above the ground row");
    check(minZ > 0.0f && maxZ < 1.0f, "mesh: z within the depth range");

    // Selection tint recolors the fence's pieces.
    const int fenceId = solo.fenceAt({10, 10});
    const std::vector<FenceFieldVertex> tinted =
        buildFenceFieldTriangles(iso, solo, meshes, fenceId);
    bool differs = false;
    for (size_t i = 0; i < tinted.size() && !differs; ++i) {
        differs = tinted[i].r != soloVerts[i].r || tinted[i].g != soloVerts[i].g ||
            tinted[i].b != soloVerts[i].b;
    }
    check(fenceId >= 0 && differs, "mesh: selection tint recolors pieces");

    // Placement contract: the piece pivot (post axis, ground) projects onto
    // the cell CENTER — mapToField returns the diamond center for integer
    // cell coords; a half-cell shift would put posts on grid nodes.
    const glm::vec3 pivot = fenceWorldToField(iso, {10.0f, 0.0f, 10.0f});
    const glm::vec2 center = iso.mapToField({10, 10});
    check(std::abs(pivot.x - center.x) < 1e-4f && std::abs(pivot.y - center.y) < 1e-4f,
        "mesh: piece pivot projects to the cell center");
}

} // namespace

bool runFenceMeshSmokeTest() {
    g_failures = 0;
    FenceMeshSet meshes;
    testMeshLoading(&meshes);
    testMeshInstancing(meshes);
    if (g_failures == 0) {
        spdlog::info("TEST PASS: fence mesh smoke (all checks)");
    } else {
        spdlog::error("TEST FAIL: fence mesh smoke, {} check(s) failed", g_failures);
    }
    return g_failures == 0;
}
