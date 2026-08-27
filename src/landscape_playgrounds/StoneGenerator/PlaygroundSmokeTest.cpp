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
        // reverse. Vertices are keyed on their exact emitted float triple, which
        // is precisely the identity the emitter welds on — a Nef snapshot
        // indexes shared corners, and corners that a float cannot tell apart are
        // dropped as slivers there. Re-rounding on top of that would only merge
        // further and report cracks in a watertight mesh.
        const auto meshClosed = [](const StoneMesh& m) {
            std::map<std::tuple<float, float, float>, int> ids;
            std::vector<int> vert;
            const std::size_t n = m.pos.size() / 3;
            vert.reserve(n);
            for (std::size_t i = 0; i < n; ++i) {
                const auto key = std::make_tuple(
                    m.pos[i * 3], m.pos[i * 3 + 1], m.pos[i * 3 + 2]);
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

        // Max y of the mesh — the stone's apex, used by the spire checks.
        const auto maxY = [](const StoneMesh& m) {
            float y = -1e30f;
            for (std::size_t i = 1; i < m.pos.size(); i += 3) {
                y = std::max(y, m.pos[i]);
            }
            return y;
        };
        // Widest XZ reach from the vertical axis among vertices inside the
        // height band [lo, hi] (fractions of the mesh height). The taper test
        // compares a low band against a high one.
        const auto bandRadius = [](const StoneMesh& m, float lo, float hi) {
            float y0 = 1e30f, y1 = -1e30f;
            for (std::size_t i = 1; i < m.pos.size(); i += 3) {
                y0 = std::min(y0, m.pos[i]);
                y1 = std::max(y1, m.pos[i]);
            }
            const float a = y0 + (y1 - y0) * lo;
            const float b = y0 + (y1 - y0) * hi;
            double best = 0.0;
            for (std::size_t i = 0; i + 2 < m.pos.size(); i += 3) {
                if (m.pos[i + 1] < a || m.pos[i + 1] > b) {
                    continue;
                }
                best = std::max(best, std::hypot(
                    static_cast<double>(m.pos[i]), static_cast<double>(m.pos[i + 2])));
            }
            return best;
        };
        // A bare prism: every macro stage off, so the oracles below stay exact
        // and each stage can be switched on one at a time.
        const auto prism = [] {
            StoneCutParams p;
            p.lobes = 1;
            p.taperCuts = 0;
            p.topSlantDeg = 0.0f;
            p.rimBevelCuts = 0;
            p.cuts = 0;
            p.grooves = 0;
            p.pits = 0;
            return p;
        };

        StoneCutParams box = prism();
        const StoneMesh boxMesh = generateCutStone(box);
        const double boxVol =
            2.0 * box.sizeX * 2.0 * box.sizeZ * static_cast<double>(box.height);
        check(
            std::abs(meshVolume(boxMesh) - boxVol) < 1e-3 * boxVol,
            "cut: box volume (exact booleans)");
        check(meshClosed(boxMesh), "cut: box mesh closed");
        check(boxMesh.triCount >= 12, "cut: box triangulated");

        StoneCutParams cutOnly = prism();
        cutOnly.cuts = 12;
        const StoneMesh cutMesh = generateCutStone(cutOnly);
        const double vCut = meshVolume(cutMesh);
        check(vCut < boxVol * 0.995 && vCut > boxVol * 0.2, "cut: corner cuts remove material");
        check(meshClosed(cutMesh), "cut: closed after corner cuts");

        StoneCutParams full = cutOnly; // explicit concavities (defaults ship them off)
        full.grooves = 2;
        full.pits = 2;
        const StoneMesh fullMesh = generateCutStone(full);
        check(meshVolume(fullMesh) < vCut, "cut: grooves and pits remove more material");
        check(meshClosed(fullMesh), "cut: closed after grooves and pits");
        check(
            minY(fullMesh) >= -full.sink - 1e-3f && minY(fullMesh) < 0.05f,
            "cut: base sits on ground");

        // --- Macro form: massif, taper, top ---------------------------------
        {
            // Lobes are unioned in, so the massif holds MORE material than its
            // main box and rests on a wider footprint — but never grows taller.
            StoneCutParams massif = prism();
            massif.lobes = 3;
            massif.lobeSpread = 0.55f;
            const StoneMesh massifMesh = generateCutStone(massif);
            check(meshVolume(massifMesh) > boxVol * 1.02, "massif: lobes add material");
            check(
                std::abs(maxY(massifMesh) - maxY(boxMesh)) < 1e-3f,
                "massif: lobes never out-top the main box");
            check(meshClosed(massifMesh), "massif: closed union");
            check(minY(massifMesh) >= -massif.sink - 1e-3f, "massif: rests on the ground");

            // Taper: with every flank leaning (no vertical walls) the top band
            // must end up strictly narrower than the base band, and — the whole
            // point of the base-safety argument — the footprint must survive
            // untouched.
            StoneCutParams taper = prism();
            taper.taperCuts = 6;
            taper.taperDeg = 18.0f;
            taper.taperWalls = 0.0f;
            const StoneMesh taperMesh = generateCutStone(taper);
            const double rLow = bandRadius(taperMesh, 0.0f, 0.2f);
            const double rHigh = bandRadius(taperMesh, 0.8f, 1.0f);
            check(rHigh < rLow * 0.92, "taper: top narrower than base");
            check(meshClosed(taperMesh), "taper: closed mesh");
            const double boxFootprint =
                4.0 * static_cast<double>(taper.sizeX) * static_cast<double>(taper.sizeZ);
            check(
                footprintArea(taperMesh) > 0.99 * boxFootprint,
                "taper: footprint untouched (base-safe by construction)");

            // Flanks left vertical really are left alone: the same recipe with
            // walls keeps more material than the all-leaning frustum above, and
            // its crown stays wider.
            StoneCutParams walls = taper;
            walls.taperWalls = 0.75f;
            const StoneMesh wallsMesh = generateCutStone(walls);
            check(
                meshVolume(wallsMesh) > meshVolume(taperMesh) * 1.02 &&
                    bandRadius(wallsMesh, 0.8f, 1.0f) > rHigh,
                "taper: vertical walls keep material the frustum loses");

            // The plateau floor: even a taper that would otherwise pinch the top
            // has to leave a crown, because a stone that comes to a point reads
            // as a shard rather than as rock.
            StoneCutParams pinch = prism();
            pinch.taperCuts = 10;
            pinch.taperDeg = 32.0f;
            pinch.taperReach = 1.0f;
            pinch.taperWalls = 0.0f;
            const StoneMesh pinchMesh = generateCutStone(pinch);
            check(
                bandRadius(pinchMesh, 0.98f, 1.0f) >
                    0.3 * std::min(pinch.sizeX, pinch.sizeZ),
                "taper: the plateau survives a pinching taper");
            check(meshClosed(pinchMesh), "taper: closed under a pinching taper");

            // A taper steep enough to reach the base if it were unclamped: the
            // reach cap still has to leave the footprint whole.
            StoneCutParams steep = taper;
            steep.taperDeg = 35.0f;
            steep.taperReach = 1.0f;
            const StoneMesh steepMesh = generateCutStone(steep);
            check(
                footprintArea(steepMesh) > 0.99 * boxFootprint,
                "taper: steep reach still spares the footprint");
            check(meshClosed(steepMesh), "taper: closed at max reach");

            // Plateau slant and rim chamfer: both anchored at the top, both
            // must leave the base alone and the mesh watertight.
            StoneCutParams top = prism();
            top.topSlantDeg = 14.0f;
            top.rimBevelCuts = 5;
            top.rimBevel = 0.25f;
            const StoneMesh topMesh = generateCutStone(top);
            check(meshVolume(topMesh) < boxVol * 0.98, "top: slant and rim remove material");
            check(
                footprintArea(topMesh) > 0.99 * boxFootprint,
                "top: plateau work spares the footprint");
            check(meshClosed(topMesh), "top: closed mesh");

            // The crown guard in the cut pass: chips are welcome on the rim, but
            // a heavy cut pass must not saw the plateau the macro stages built
            // down to a point — that was how a boulder turned into a roofed shard.
            StoneCutParams crowned = prism();
            crowned.taperCuts = 5;
            crowned.taperWalls = 0.0f;
            crowned.rimBevelCuts = 4;
            StoneCutParams chipped = crowned;
            chipped.cuts = 24;
            const StoneMesh chippedMesh = generateCutStone(chipped);
            check(
                std::abs(maxY(chippedMesh) - maxY(generateCutStone(crowned))) < 1e-3f,
                "cut: chips leave the plateau at its height");
            check(
                bandRadius(chippedMesh, 0.95f, 1.0f) >
                    0.3 * std::min(chipped.sizeX, chipped.sizeZ),
                "cut: chips spare the plateau");
            check(meshClosed(chippedMesh), "cut: closed after a heavy chip pass");

            // The shipping recipe: all stages on at once.
            const StoneMesh shipped = generateCutStone(StoneCutParams{});
            check(meshClosed(shipped), "defaults: closed mesh");
            check(
                minY(shipped) >= -StoneCutParams{}.sink - 1e-3f && minY(shipped) < 0.05f,
                "defaults: base sits on ground");
            check(
                bandRadius(shipped, 0.8f, 1.0f) < bandRadius(shipped, 0.0f, 0.2f),
                "defaults: silhouette tapers upward");
        }

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
            pp.big = prism(); // macro stages off: both stones stay plain boxes
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
            // Prism baseline: with the macro stages off the starting footprint
            // IS the box footprint, so the area comparisons below measure the
            // nick filters and nothing else.
            StoneCutParams strict = prism();
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

            // The quota is the hard gate: at zero, no cut may reach the base at
            // all, so the footprint has to come out untouched. How much the
            // steep filter alone spares depends on which corners the rng picks,
            // so that one is only checked comparatively, against the lenient run.
            StoneCutParams noNicks = strict;
            noNicks.baseCutQuota = 0.0f;
            check(
                std::abs(footprintArea(generateCutStone(noNicks)) - boxFoot) < 1e-3 * boxFoot,
                "base nicks: zero quota leaves the footprint whole");
            check(
                fpLenient < fpStrict - 0.02 * boxFoot,
                "base nicks: lenient filter chips the base");
            check(
                fpLenient > lenient.baseMinArea * boxFoot - 1e-3,
                "base nicks: support area floor");
            check(minY(lenientMesh) >= -lenient.sink - 1e-3f, "base nicks: still on the ground");
            check(meshClosed(lenientMesh), "base nicks: closed mesh");
        }

        // Cluster: a fan of companions with tight imprints, plus debris.
        {
            StoneClusterParams cp;
            cp.base = prism();
            cp.base.cuts = 8;
            cp.levels = 1;
            cp.maxChildren = 1;
            cp.spread = 0.0f;      // every child on the front bisector exactly
            cp.spireChance = 0.0f; // no child out-tops the root, none go behind
            cp.pebbles = 0;
            const StoneMesh cluster1 = generateCutStoneCluster(cp);
            check(cluster1.triCount > 0 && meshClosed(cluster1), "cluster: closed mesh");
            check(generateCutStoneCluster(cp).pos == cluster1.pos, "cluster: deterministic");

            // Front rule at spread=0: children sit on the +X+Z bisector, so
            // anything reaching past the root's bbox must be in that quadrant —
            // nothing is ever placed where the root's silhouette would hide it.
            const double sx1 = cp.base.sizeX, sz1 = cp.base.sizeZ;
            bool frontOk = true;
            for (std::size_t i = 0; i + 2 < cluster1.pos.size(); i += 3) {
                const double x = cluster1.pos[i], z = cluster1.pos[i + 2];
                if (x > sx1 + 1e-3 && z < 0.0) {
                    frontOk = false;
                }
                if (z > sz1 + 1e-3 && x < 0.0) {
                    frontOk = false;
                }
            }
            check(frontOk, "cluster: companions stay in the front quadrant (spread 0)");

            // Spires are the knob that breaks the decaying stair: off, the root
            // owns the apex; forced on, the group grows past it.
            const StoneMesh rootSolo = generateCutStone(cp.base);
            check(
                maxY(cluster1) <= maxY(rootSolo) + 1e-3f,
                "cluster: no spires, the root keeps the apex");

            StoneClusterParams spires = cp;
            spires.spireChance = 1.0f;
            const StoneMesh spireMesh = generateCutStoneCluster(spires);
            check(
                maxY(spireMesh) > maxY(rootSolo) + 1e-3f, "cluster: spires out-top the root");
            check(meshClosed(spireMesh), "cluster: closed with spires");

            // Debris sits beyond the cluster's support radius: it adds stones
            // without touching any of them, and it stands on the ground.
            StoneClusterParams debris = cp;
            debris.pebbles = 6;
            const StoneMesh debrisMesh = generateCutStoneCluster(debris);
            check(debrisMesh.triCount > cluster1.triCount, "cluster: debris adds stones");
            check(meshClosed(debrisMesh), "cluster: closed with debris");
            check(
                minY(debrisMesh) >= -cp.base.sink - 1e-3f,
                "cluster: debris rests on the ground");

            StoneClusterParams deep = cp;
            deep.levels = 2;
            deep.maxChildren = 2;
            const StoneMesh cluster2 = generateCutStoneCluster(deep);
            check(cluster2.triCount > cluster1.triCount, "cluster: recursion adds stones");
            check(meshClosed(cluster2), "cluster: closed at depth 2");
        }
    }

    if (failures == 0) {
        spdlog::info("TEST PASS: StoneGenerator smoke (all checks)");
    } else {
        spdlog::error("TEST FAIL: StoneGenerator smoke, {} check(s) failed", failures);
    }
    return failures == 0;
}
