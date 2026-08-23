#include "pch.h"

#include "PlaygroundSmokeTest.h"

#include <cmath>

#include <spdlog/spdlog.h>

#include <topology_core/camera2d.h>
#include <topology_core/diamond_isometry.h>

#include "NodeField.h"
#include "StoneCut.h"

#include <algorithm>
#include <map>
#include <set>
#include <tuple>
#include <vector>

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

    // --- Cut generator (CGAL Nef exact booleans) -------------------------------
    {
        // Signed volume of the emitted triangle soup (divergence theorem).
        const auto meshVolume = [](const StoneMesh& m) {
            double v = 0.0;
            for (int t = 0; t < m.triCount; ++t) {
                const glm::dvec3 a{m.pos[t * 9 + 0], m.pos[t * 9 + 1], m.pos[t * 9 + 2]};
                const glm::dvec3 b{m.pos[t * 9 + 3], m.pos[t * 9 + 4], m.pos[t * 9 + 5]};
                const glm::dvec3 cc{m.pos[t * 9 + 6], m.pos[t * 9 + 7], m.pos[t * 9 + 8]};
                v += glm::dot(a, glm::cross(b, cc));
            }
            return v / 6.0;
        };
        // Closedness: every directed edge appears exactly once, matched by its
        // reverse, over quantized (1e-6) vertex positions.
        const auto meshClosed = [](const StoneMesh& m) {
            std::map<std::tuple<long long, long long, long long>, int> ids;
            std::vector<int> vert;
            const std::size_t n = m.pos.size() / 3;
            vert.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                const auto key = std::make_tuple(
                    static_cast<long long>(std::llround(static_cast<double>(m.pos[i * 3]) * 1e6)),
                    static_cast<long long>(std::llround(static_cast<double>(m.pos[i * 3 + 1]) * 1e6)),
                    static_cast<long long>(std::llround(static_cast<double>(m.pos[i * 3 + 2]) * 1e6)));
                const auto [it, added] = ids.try_emplace(key, static_cast<int>(ids.size()));
                vert.push_back(it->second);
            }
            std::map<std::pair<int, int>, int> directed;
            for (int t = 0; t < m.triCount; ++t) {
                const int a = vert[t * 3];
                const int b = vert[t * 3 + 1];
                const int cc = vert[t * 3 + 2];
                directed[{a, b}]++;
                directed[{b, cc}]++;
                directed[{cc, a}]++;
            }
            for (const auto& [edge, count] : directed) {
                if (count != 1) {
                    return false;
                }
                const auto it = directed.find({edge.second, edge.first});
                if (it == directed.end() || it->second != 1) {
                    return false;
                }
            }
            return true;
        };
        const auto minY = [](const StoneMesh& m) {
            float y = 1e30f;
            for (std::size_t i = 1; i < m.pos.size(); i += 3) {
                y = std::min(y, m.pos[i]);
            }
            return y;
        };
        // Footprint area on the ground plane: unique near-ground vertices,
        // angularly sorted around their centroid (exact for convex, fine for
        // the star-shaped nicked footprints here), shoelace.
        const auto footprintArea = [](const StoneMesh& m) {
            float gy = 1e30f;
            for (std::size_t i = 1; i < m.pos.size(); i += 3) {
                gy = std::min(gy, m.pos[i]);
            }
            std::set<std::pair<long long, long long>> uniq;
            for (std::size_t i = 0; i + 2 < m.pos.size(); i += 3) {
                if (m.pos[i + 1] < gy + 1e-3f) {
                    uniq.emplace(
                        std::llround(static_cast<double>(m.pos[i]) * 1e5),
                        std::llround(static_cast<double>(m.pos[i + 2]) * 1e5));
                }
            }
            std::vector<std::pair<double, double>> pts;
            for (const auto& k : uniq) {
                pts.emplace_back(k.first / 1e5, k.second / 1e5);
            }
            if (pts.size() < 3) {
                return 0.0;
            }
            double cx = 0.0, cz = 0.0;
            for (const auto& p : pts) {
                cx += p.first;
                cz += p.second;
            }
            cx /= static_cast<double>(pts.size());
            cz /= static_cast<double>(pts.size());
            std::sort(pts.begin(), pts.end(), [&](const auto& a, const auto& b) {
                return std::atan2(a.second - cz, a.first - cx) <
                    std::atan2(b.second - cz, b.first - cx);
            });
            double area = 0.0;
            for (std::size_t i = 0; i < pts.size(); ++i) {
                const auto& u = pts[i];
                const auto& w = pts[(i + 1) % pts.size()];
                area += u.first * w.second - w.first * u.second;
            }
            return std::abs(area) * 0.5;
        };

        StoneCutParams box;
        box.cuts = 0;
        box.grooves = 0;
        box.pits = 0;
        const StoneMesh boxMesh = generateCutStone(box);
        const double boxVol =
            2.0 * box.sizeX * 2.0 * box.sizeZ * static_cast<double>(box.height);
        check(
            std::abs(meshVolume(boxMesh) - boxVol) < 1e-3 * boxVol,
            "cut: box volume (exact booleans)");
        check(meshClosed(boxMesh), "cut: box mesh closed");
        check(boxMesh.triCount >= 12, "cut: box triangulated");

        StoneCutParams cutOnly;
        cutOnly.grooves = 0;
        cutOnly.pits = 0;
        const StoneMesh cutMesh = generateCutStone(cutOnly);
        const double vCut = meshVolume(cutMesh);
        check(vCut < boxVol * 0.995 && vCut > boxVol * 0.2, "cut: corner cuts remove material");
        check(meshClosed(cutMesh), "cut: closed after corner cuts");

        StoneCutParams full; // explicit concavities (defaults ship grooves/pits off)
        full.grooves = 2;
        full.pits = 2;
        const StoneMesh fullMesh = generateCutStone(full);
        check(meshVolume(fullMesh) < vCut, "cut: grooves and pits remove more material");
        check(meshClosed(fullMesh), "cut: closed after grooves and pits");
        check(
            minY(fullMesh) >= -full.sink - 1e-3f && minY(fullMesh) < 0.05f,
            "cut: base sits on ground");

        // Capped grooves hit CGAL tangent-contact asserts historically; the
        // try/perturb retry must ride over them and still produce a closed mesh.
        StoneCutParams capped;
        capped.grooves = 4;
        capped.grooveLen = 0.6f;
        capped.pits = 4;
        const StoneMesh cappedMesh = generateCutStone(capped);
        check(cappedMesh.triCount > 0 && meshClosed(cappedMesh), "cut: capped grooves, closed mesh");

        StoneCutParams det;
        det.cuts = 16;
        det.seed = 21;
        const StoneMesh da = generateCutStone(det);
        const StoneMesh db = generateCutStone(det);
        check(da.pos == db.pos && da.col == db.col, "cut: same seed, identical mesh");
        det.seed = 22;
        check(generateCutStone(det).pos != da.pos, "cut: different seed, different mesh");

        // Companion pair, zero cuts: the carve is box-minus-inflated-box, and
        // the intersection of two axis-aligned boxes is itself a box — an
        // exact analytic volume oracle for the Nef subtraction.
        {
            StonePairParams pp;
            pp.big.cuts = 0;
            pp.big.grooves = 0;
            pp.big.pits = 0;
            pp.small = pp.big;
            pp.small.seed = 777;
            pp.small.sizeX = pp.big.sizeX * 0.6f;
            pp.small.sizeZ = pp.big.sizeZ * 0.6f;
            pp.small.height = pp.big.height * 0.6f;
            const StoneMesh pairMesh = generateCutStonePair(pp);
            const StoneMesh bigSolo = generateCutStone(pp.big);

            const double sx1 = pp.big.sizeX, sz1 = pp.big.sizeZ, h1 = pp.big.height;
            const double sx2 = pp.small.sizeX, sz2 = pp.small.sizeZ, h2 = pp.small.height;
            const double s = 1.0 + pp.gap / sx1; // inflation factor about (0, h1/2, 0)
            const double xSpan = (s * sx1) - (sx1 - pp.overlap);
            const double yLo = std::max(0.0, 0.5 * h1 * (1.0 - s));
            const double yHi = std::min(h2, 0.5 * h1 * (1.0 + s));
            const double zHalf = std::min(s * sz1, sz2);
            const double vIntersect = xSpan * (yHi - yLo) * 2.0 * zHalf;
            const double vSmallBox = 2.0 * sx2 * 2.0 * sz2 * h2;
            const double vExpect = meshVolume(bigSolo) + (vSmallBox - vIntersect);
            check(
                std::abs(meshVolume(pairMesh) - vExpect) < 1e-3 * vExpect,
                "pair: carve volume matches box-intersection oracle");
            check(meshClosed(pairMesh), "pair: closed mesh (both stones)");
            check(generateCutStonePair(pp).pos == pairMesh.pos, "pair: deterministic");
        }

        // Pair with cuts on: the companion adds volume over the big stone
        // alone and the merged soup stays watertight.
        {
            StonePairParams pp;
            pp.small = pp.big;
            pp.small.seed = 1234;
            pp.small.sizeX = pp.big.sizeX * 0.6f;
            pp.small.sizeZ = pp.big.sizeZ * 0.6f;
            pp.small.height = pp.big.height * 0.6f;
            const StoneMesh pairCut = generateCutStonePair(pp);
            const double vBig = meshVolume(generateCutStone(pp.big));
            check(meshVolume(pairCut) > vBig * 1.05, "pair: companion adds volume");
            check(meshClosed(pairCut), "pair: closed with cuts on");
        }

        // Base nicks: the steepness filter gates how much of the footprint the
        // cuts may chip; the support floor keeps the stone standing.
        {
            StoneCutParams strict;
            strict.cuts = 24;
            strict.baseCutQuota = 0.5f;
            strict.baseCutAngleDeg = 80.0f; // near-vertical planes only
            StoneCutParams lenient = strict;
            lenient.baseCutAngleDeg = 25.0f; // grazing planes admitted
            const double boxFoot =
                4.0 * static_cast<double>(strict.sizeX) * static_cast<double>(strict.sizeZ);
            const StoneMesh strictMesh = generateCutStone(strict);
            const StoneMesh lenientMesh = generateCutStone(lenient);
            const double fpStrict = footprintArea(strictMesh);
            const double fpLenient = footprintArea(lenientMesh);
            check(fpStrict > 0.90 * boxFoot, "base nicks: steep filter keeps the footprint");
            check(
                fpLenient < fpStrict - 0.02 * boxFoot,
                "base nicks: lenient filter chips the base");
            check(
                fpLenient > lenient.baseMinArea * boxFoot - 1e-3,
                "base nicks: support area floor");
            check(minY(lenientMesh) >= -lenient.sink - 1e-3f, "base nicks: still on the ground");
            check(meshClosed(lenientMesh), "base nicks: closed mesh");
        }
    }

    if (failures == 0) {
        spdlog::info("TEST PASS: StoneGenerator smoke (all checks)");
    } else {
        spdlog::error("TEST FAIL: StoneGenerator smoke, {} check(s) failed", failures);
    }
    return failures == 0;
}
