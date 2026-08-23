#include "pch.h"

#include "PlaygroundSmokeTest.h"

#include <cmath>

#include <spdlog/spdlog.h>

#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include "NodeField.h"
#include "StoneGen.h"
#include "StonePoly.h"

namespace {

constexpr int kMapW = 24;
constexpr int kMapH = 24;

} // namespace

bool runStoneGeneratorSmokeTest() {
    int failures = 0;
    const auto check = [&failures](bool ok, const char* name) {
        if (ok) {
            spdlog::info("TEST PASS: {}", name);
        } else {
            spdlog::error("TEST FAIL: {}", name);
            ++failures;
        }
    };

    const topology_core::DiamondIsometry iso;

    // mapToField/fieldToMap round trip over the whole canvas.
    bool cellsOk = true;
    for (int y = 0; y < kMapH && cellsOk; ++y) {
        for (int x = 0; x < kMapW && cellsOk; ++x) {
            cellsOk = iso.fieldToMap(iso.mapToField({x, y})) == glm::ivec2(x, y);
        }
    }
    check(cellsOk, "cell projection round trip");

    // nodeToField/fieldToNode round trip, border nodes included.
    bool nodesOk = true;
    for (int y = 0; y <= kMapH && nodesOk; ++y) {
        for (int x = 0; x <= kMapW && nodesOk; ++x) {
            nodesOk = iso.fieldToNode(iso.nodeToField({x, y})) == glm::ivec2(x, y);
        }
    }
    check(nodesOk, "node projection round trip");

    // Vertex-node contract: a node is the Up corner of the cell with the
    // same coordinates.
    const auto corners = iso.cellDiamondCorners({7, 9}); // Left, Up, Right, Down
    check(corners[1] == iso.nodeToField({7, 9}), "node == cell Up corner");

    const glm::vec2 c = iso.mapToField({7, 9});
    const bool shapeOk = corners[0].x < c.x && corners[2].x > c.x &&
        corners[1].y < c.y && corners[3].y > c.y &&
        std::abs(corners[0].y - c.y) < 1e-4f && std::abs(corners[2].y - c.y) < 1e-4f &&
        std::abs(corners[1].x - c.x) < 1e-4f && std::abs(corners[3].x - c.x) < 1e-4f;
    check(shapeOk, "diamond corner geometry");

    bool neighboursOk = true;
    for (const glm::ivec2 cell : topology_core::DiamondIsometry::nodeNeighbourCells({5, 6})) {
        const auto nodes = topology_core::DiamondIsometry::cellCornerNodes(cell);
        bool found = false;
        for (const glm::ivec2 n : nodes) {
            found = found || (n == glm::ivec2(5, 6));
        }
        neighboursOk = neighboursOk && found;
    }
    check(neighboursOk, "node neighbour cells contain the node");

    topology_core::Camera2D cam;
    cam.offset = {123.0f, -45.0f};
    cam.zoom = 1.75f;
    const glm::vec2 p{640.0f, 320.0f};
    const glm::vec2 roundTrip = cam.worldToScreen(cam.screenToWorld(p));
    check(glm::length(roundTrip - p) < 1e-3f, "camera screen/world inverse");

    // --- Node field contract -------------------------------------------------
    {
        NodeField field;
        field.reset(25, 25);
        check(field.width == 25 && field.height == 25 && field.onNodeCount() == 0,
            "node field reset");

        paintNodeLine(field, {2, 3}, {9, 3}, true);
        bool lineOk = true;
        for (int x = 2; x <= 9; ++x) {
            lineOk = lineOk && field.isOn({x, 3});
        }
        check(lineOk && field.onNodeCount() == 8, "node line painting (horizontal)");

        NodeField diag;
        diag.reset(25, 25);
        paintNodeLine(diag, {2, 2}, {9, 9}, true);
        check(diag.onNodeCount() == 8, "node line painting (diagonal, no holes)");

        NodeField versioned;
        versioned.reset(25, 25);
        const std::uint64_t v0 = versioned.version;
        versioned.setNode({5, 5}, true);
        versioned.setNode({5, 5}, true);
        check(versioned.version == v0 + 1, "node version bumps only on changes");
    }

    // --- Stone generator (polyhedral clipper) -----------------------------------
    {
        // Clean params: exact face counts without jitter/noise.
        const auto cleanParams = [](StoneBaseForm form) {
            StoneGenParams p;
            p.form = form;
            p.chamferWidth = 0.0f;
            p.planeTiltDeg = 0.0f;
            p.planeOffset = 0.0f;
            p.noiseAmp = 0.0f;
            p.tintJitter = 0.0f;
            p.shapeVariance = 0.0f;
            return p;
        };

        StonePoly poly;
        std::vector<StonePlane> planes;
        StoneGenParams p = cleanParams(StoneBaseForm::Box);
        buildStonePoly(p, poly, planes);
        check(poly.faces.size() == 6, "stone: box face count");

        p.chamferWidth = 0.12f;
        p.chamferTopOnly = false;
        buildStonePoly(p, poly, planes);
        check(poly.faces.size() == 18, "stone: chamfered box face count (all edges)");

        p.chamferTopOnly = true;
        buildStonePoly(p, poly, planes);
        check(poly.faces.size() == 10, "stone: chamfered box face count (top only)");

        p = cleanParams(StoneBaseForm::Prism);
        p.sides = 6;
        buildStonePoly(p, poly, planes);
        check(poly.faces.size() == 8, "stone: hex prism face count");

        // Ball factory directly (no lift/ground clip in the way): with zero
        // offset jitter every tangent plane is extreme, so faces == planes.
        std::mt19937_64 rng(42);
        const std::vector<StonePlane> ballPlanes = makeBallPlanes(24, 1.2, 0.0, rng);
        poly = buildPolyhedron(ballPlanes);
        check(poly.faces.size() == 24, "stone: ball face count (jitter 0)");

        // Full-defaults invariants for every form.
        const std::pair<StoneBaseForm, const char*> forms[] = {
            {StoneBaseForm::Box, "box"},
            {StoneBaseForm::Frustum, "frustum"},
            {StoneBaseForm::Prism, "prism"},
            {StoneBaseForm::Sphere, "sphere"},
            {StoneBaseForm::Oval, "oval"},
        };
        for (const auto& [form, name] : forms) {
            StoneGenParams dp;
            dp.form = form;
            buildStonePoly(dp, poly, planes);
            check(isWatertight(poly), (std::string("stone: ") + name + " watertight").c_str());

            // The clip invariant holds before vertex noise (noise moves shared
            // verts across their planes by design).
            dp.noiseAmp = 0.0f;
            buildStonePoly(dp, poly, planes);
            check(
                allInside(poly, planes, 1e-6),
                (std::string("stone: ") + name + " clip invariant").c_str());

            const StoneMesh mesh = generateStone(dp);
            float minY = 1e30f;
            bool normalsOk = mesh.triCount > 0;
            for (int i = 0; i < mesh.triCount * 3; ++i) {
                minY = std::min(minY, mesh.pos[i * 3 + 1]);
                const float nx = mesh.nrm[i * 3];
                const float ny = mesh.nrm[i * 3 + 1];
                const float nz = mesh.nrm[i * 3 + 2];
                const float len = std::sqrt(nx * nx + ny * ny + nz * nz);
                normalsOk = normalsOk && std::abs(len - 1.0f) < 1e-3f && std::isfinite(len);
            }
            check(normalsOk, (std::string("stone: ") + name + " mesh normals").c_str());
            check(
                minY >= -dp.sink - 1e-3f && minY < 0.05f,
                (std::string("stone: ") + name + " base sits on ground").c_str());
        }

        // The ground clip cuts a sphere exactly at -sink.
        StoneGenParams sp;
        sp.form = StoneBaseForm::Sphere;
        const StoneMesh sphereMesh = generateStone(sp);
        float sphereMinY = 1e30f;
        for (size_t i = 1; i < sphereMesh.pos.size(); i += 3) {
            sphereMinY = std::min(sphereMinY, sphereMesh.pos[i]);
        }
        check(std::abs(sphereMinY + sp.sink) < 1e-3f, "stone: sphere base clipped at -sink");

        // Seed determinism (bit-identical buffers).
        StoneGenParams det;
        det.form = StoneBaseForm::Frustum;
        det.seed = 7;
        const StoneMesh a = generateStone(det);
        const StoneMesh b = generateStone(det);
        check(a.pos == b.pos && a.col == b.col, "stone: same seed, identical mesh");
        det.seed = 8;
        const StoneMesh c = generateStone(det);
        check(c.pos != a.pos, "stone: different seed, different mesh");
    }

    if (failures == 0) {
        spdlog::info("TEST PASS: StoneGenerator smoke (all checks)");
    } else {
        spdlog::error("TEST FAIL: StoneGenerator smoke, {} check(s) failed", failures);
    }
    return failures == 0;
}
