#include "pch.h"

#include "SmokeTest.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <pgg/eval.h>
#include <pgg/pgg.h>
#include <pgg/src/eval/expand.h>
#include <pgg/src/eval/modules.h>
#include <pgg/src/graph.h>
#include <pgg/src/layout.h>

#include "FrameCompare.h"
#include "GeometryPreview.h"
#include "ViewerRpcServer.h"

#if defined(_WIN32)
    #include <winsock2.h>
    #include <ws2tcpip.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
    #include <sys/time.h>
    #include <unistd.h>
#endif

namespace {

int g_failures = 0;

void check(bool ok, const char* name) {
    if (ok) {
        spdlog::info("TEST PASS: {}", name);
    } else {
        spdlog::error("TEST FAIL: {}", name);
        ++g_failures;
    }
}

// Walks up from the cwd looking for the repository root (.git marker) — the
// same convention fence_core::findRepoRoot uses, duplicated here so the
// viewer does not depend on fence_core.
std::string findRepoRoot() {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::current_path(ec);
    if (ec) return ".";
    for (int i = 0; i < 12; ++i) {
        if (std::filesystem::exists(dir / ".git", ec)) return dir.string();
        if (!dir.has_parent_path() || dir == dir.parent_path()) break;
        dir = dir.parent_path();
    }
    return ".";
}

const pgg::GraphNode* definingNode(const pgg::GraphScope& g, const std::string& name) {
    for (const pgg::GraphNode& n : g.nodes)
        for (const std::string& out : n.outputs)
            if (out == name) return &n;
    return nullptr;
}

bool hasEdge(const pgg::GraphScope& g, int from, int to, bool loop) {
    for (const pgg::GraphEdge& e : g.edges)
        if (e.fromNode == from && e.toNode == to && e.loop == loop) return true;
    return false;
}

bool layoutsEqual(const pgg::GraphProject& a, const pgg::GraphProject& b) {
    if (a.top.nodes.size() != b.top.nodes.size() || a.instanceScopes.size() != b.instanceScopes.size())
        return false;
    auto scopeEq = [](const pgg::GraphScope& x, const pgg::GraphScope& y) {
        if (x.nodes.size() != y.nodes.size() || x.zones.size() != y.zones.size()) return false;
        for (size_t i = 0; i < x.nodes.size(); ++i)
            if (x.nodes[i].x != y.nodes[i].x || x.nodes[i].y != y.nodes[i].y ||
                x.nodes[i].layer != y.nodes[i].layer)
                return false;
        for (size_t i = 0; i < x.zones.size(); ++i)
            if (x.zones[i].x != y.zones[i].x || x.zones[i].w != y.zones[i].w) return false;
        return true;
    };
    if (!scopeEq(a.top, b.top)) return false;
    for (size_t i = 0; i < a.instanceScopes.size(); ++i)
        if (!scopeEq(a.instanceScopes[i], b.instanceScopes[i])) return false;
    return true;
}

}  // namespace

bool runPggViewerSmokeTest(const std::string& serveAddress) {
    g_failures = 0;
    const std::string corpus = findRepoRoot() + "/src/tests/pgg/corpus";

    // 1. tower.pgg: instance paths and dive targets (the §16 composition).
    {
        pgg::Document doc = pgg::parseFile(corpus + "/tower.pgg");
        check(!doc.hasErrors(), "tower.pgg parses");
        pgg::GraphProject p = pgg::buildGraph(doc);
        const std::vector<std::string> expected = {
            "cliff_wall[0]",
            "cliff_wall[0].make_rock_sdf[0]",
            "make_rock[0]",
            "make_rock[0].make_rock_sdf[1]",
            "make_rock[0].fbm_displace[0]",
        };
        check(p.instancePaths == expected, "tower instance paths (global counters)");
        const pgg::GraphNode* wall = definingNode(p.top, "wall");
        check(wall && wall->kind == pgg::GraphNode::Kind::DefCall && wall->instanceName == "cliff_wall[0]",
              "tower def-call node");
        const pgg::GraphScope* body = p.scopeOf("make_rock[0]");
        check(body && definingNode(*body, "src") &&
                  definingNode(*body, "src")->instancePath == "make_rock[0].make_rock_sdf[1]",
              "tower nested dive scope");
    }

    // 2. The instance numbering cross-check against FlatProgram (mandatory).
    {
        pgg::Document doc = pgg::parseFile(corpus + "/tower.pgg");
        std::vector<pgg::Diagnostic> diags;
        pgg::FlatProgram flat = pgg::expandProgram(*doc.file, nullptr, diags);
        pgg::GraphProject p = pgg::buildGraph(doc);
        bool same = p.instancePaths.size() == flat.instances.size();
        for (size_t i = 0; same && i < flat.instances.size(); ++i)
            same = p.instancePaths[i] == flat.instances[i].path;
        check(same, "tower instance paths == FlatProgram");
    }

    // 3. e7_fracture.pgg: the foreach zone subgraph with ports and the loop.
    {
        pgg::Document doc = pgg::parseFile(corpus + "/e7_fracture.pgg");
        check(!doc.hasErrors(), "e7_fracture.pgg parses");
        pgg::GraphProject p = pgg::buildGraph(doc);
        bool zoneOk = p.top.zones.size() == 1;
        if (zoneOk) {
            const pgg::GraphZone& z = p.top.zones[0];
            zoneOk = z.inputPorts.size() == 1 && z.outputPorts.size() == 1 &&
                     p.top.nodes[z.header].op == "foreach" &&
                     p.top.nodes[z.inputPorts[0]].name == "piece" &&
                     hasEdge(p.top, z.outputPorts[0], z.inputPorts[0], true) &&
                     hasEdge(p.top, z.header, z.inputPorts[0], false);
        }
        check(zoneOk, "foreach zone subgraph with state loop");
    }

    // 4. Import closure: qualified calls number like the expansion does.
    {
        const std::string src =
            "import lib.rocks\n"
            "r = rng_from_seed(1)\n"
            "f, s = rocks.pebble_pair(size = 1.0, rng = r)\n"
            "output f\n";
        pgg::Document doc = pgg::parse(src, "<smoke-import>");
        std::vector<pgg::Diagnostic> diags;
        pgg::ModuleClosure closure = pgg::loadModuleClosure(*doc.file, {corpus}, diags);
        pgg::GraphProject p = pgg::buildGraph(doc, &closure);
        check(p.instancePaths ==
                  (std::vector<std::string>{"pebble_pair[0]", "pebble_pair[0].make_pebble[0]"}),
              "imported def calls number through the closure");
    }

    // 5. Layout determinism on the corpus etalons.
    {
        bool det = true;
        for (const char* file : {"tower.pgg", "e7_fracture.pgg", "e7_repeat_settle.pgg"}) {
            pgg::GraphProject a = pgg::buildGraph(pgg::parseFile(corpus + "/" + file));
            pgg::GraphProject b = pgg::buildGraph(pgg::parseFile(corpus + "/" + file));
            pgg::layoutProject(a);
            pgg::layoutProject(b);
            det = det && layoutsEqual(a, b);
        }
        check(det, "layout is deterministic across runs");
    }

    // 6. Hint parse + write-back round-trip (comments never touch the AST).
    {
        int x = 0, y = 0;
        const bool parseOk = pgg::parsePosHint("note @pos -40 12 done", x, y) && x == -40 && y == 12 &&
                             !pgg::parsePosHint("@pos abc", x, y) && !pgg::parsePosHint("@pos 1", x, y);
        check(parseOk, "hint parse (valid/dirty)");
        const std::string src =
            "g = rng_from_seed(1)\n"
            "base = ico_sphere(subdiv = 1, radius = 1.0)  # @pos 10 20\n"
            "output base\n";
        pgg::Document before = pgg::parse(src, "<smoke-hint>");
        const std::string appended = pgg::applyPosHint(src, 1, 500, 600);
        const std::string replaced = pgg::applyPosHint(appended, 2, -5, 7);
        pgg::Document after = pgg::parse(replaced, "<smoke-hint>");
        const bool roundTrip = !after.hasErrors() && pgg::astEqual(before.file, after.file) &&
                               replaced.find("# @pos -5 7") != std::string::npos &&
                               replaced.find("# @pos 500 600") != std::string::npos &&
                               pgg::applyPosHint(replaced, 2, -5, 7) == replaced;
        check(roundTrip, "hint write-back round-trip (append + replace + astEqual)");
    }

    // 7. Geometry preview (CPU half): value pulls of a mid-graph binding and
    //    the value -> triangles conversion for mesh / sdf / points.
    {
        const std::string src =
            "g = rng_from_seed(3)\n"
            "base = ico_sphere(subdiv = 2, radius = 1.0)\n"
            "m = mark(base, \"top\", where = dot(@N, (0, 1, 0)) > 0.5)\n"
            "field = sdf_sphere(r = 1.0)\n"
            "pts = mesh_line(count = 5, length = 4.0)\n"
            "rock = mesh_from_sdf(field, voxel = 0.5)\n"
            "output rock\n";
        pgg::RunParams rp;
        rp.pulls = {"m", "field", "pts"};
        pgg::RunResult r = pgg::run(src, rp, {}, "<smoke-preview>");
        check(!r.hasErrors() && r.outputs.empty() && r.pulled.size() == 3, "value pulls (3 targets, outputs suppressed)");
        if (r.pulled.size() == 3) {
            PreviewBuildOptions opts;
            opts.highlightGroup = "points:top";
            opts.sdfResolution = 24;
            const PreviewGeometry mesh = buildPreviewGeometry(r.pulled[0].value, opts);
            size_t lit = 0;
            for (const PreviewVertex& v : mesh.vertices) lit += v.mask > 0.5f ? 1 : 0;
            check(mesh.ok && mesh.indices.size() % 3 == 0 && mesh.vertices.size() == 162 && lit > 0 &&
                      lit < mesh.vertices.size() && mesh.groups == std::vector<std::string>{"points:top"} &&
                      std::abs(mesh.bmax.y - 1.0f) < 1e-3f,
                  "preview mesh (smooth indexed, group highlight, bbox)");
            const PreviewGeometry sdf = buildPreviewGeometry(r.pulled[1].value, opts);
            check(sdf.ok && sdf.indices.size() % 3 == 0 && sdf.summary.rfind("sdf (preview voxel", 0) == 0 &&
                      std::abs(sdf.bmax.x - 1.0f) < 0.15f && std::abs(sdf.bmin.x + 1.0f) < 0.15f,
                  "preview sdf (meshed at preview voxel, unit sphere bbox)");
            const PreviewGeometry pts = buildPreviewGeometry(r.pulled[2].value, opts);
            check(pts.ok && pts.vertices.size() == 5 * 24 && pts.indices.size() == 5 * 24 &&
                      pts.summary == "points 5 pts",
                  "preview points (octahedron markers)");
        }
        PreviewBuildOptions opts;
        const PreviewGeometry none = buildPreviewGeometry(pgg::Value(1.5f), opts);
        check(!none.ok && none.summary.find("no geometry") != std::string::npos, "preview of a scalar reports no geometry");
    }

    // 8. Preview shading modes on a welded box: auto/smooth -> indexed with
    //    smooth @N (8 vertices, outward diagonals), flat -> face normals per
    //    triangle, auto with compute_normals(flat) -> corner N per corner.
    {
        const std::string src =
            "b = box(size = (2, 4, 6))\n"
            "f = compute_normals(b, mode = flat)\n"
            "output f\n";
        pgg::RunParams rp;
        rp.pulls = {"b", "f"};
        pgg::RunResult r = pgg::run(src, rp, {}, "<smoke-shading>");
        check(!r.hasErrors() && r.pulled.size() == 2, "value pulls for shading modes");
        if (r.pulled.size() == 2) {
            auto axisAligned = [](const glm::vec3& n) {
                const glm::vec3 a = glm::abs(n);
                return std::abs(std::max(a.x, std::max(a.y, a.z)) - 1.0f) < 1e-4f;
            };
            PreviewBuildOptions opts;
            opts.shading = PreviewShading::Smooth;
            const PreviewGeometry sm = buildPreviewGeometry(r.pulled[0].value, opts);
            bool outward = sm.vertices.size() == 8;
            for (const PreviewVertex& v : sm.vertices)
                outward = outward && glm::dot(v.normal, glm::normalize(v.pos)) > 0.5f && !axisAligned(v.normal);
            check(sm.ok && outward && sm.indices.size() == 36, "shading smooth: 8 indexed vertices, diagonal outward @N");

            opts.shading = PreviewShading::Flat;
            const PreviewGeometry fl = buildPreviewGeometry(r.pulled[0].value, opts);
            bool faceted = fl.vertices.size() == 36;
            for (const PreviewVertex& v : fl.vertices) faceted = faceted && axisAligned(v.normal);
            check(fl.ok && faceted, "shading flat: per-triangle face normals, axis-aligned");

            opts.shading = PreviewShading::Auto;
            const PreviewGeometry autoBox = buildPreviewGeometry(r.pulled[0].value, opts);
            check(autoBox.ok && autoBox.vertices.size() == 8, "shading auto without corner N falls back to smooth @N");
            const PreviewGeometry autoFlat = buildPreviewGeometry(r.pulled[1].value, opts);
            bool cornerFaceted = autoFlat.vertices.size() == 36;
            for (const PreviewVertex& v : autoFlat.vertices) cornerFaceted = cornerFaceted && axisAligned(v.normal);
            check(autoFlat.ok && cornerFaceted, "shading auto prefers corner N from compute_normals(flat)");
        }
    }

    // 9. Camera targeting (A2): per-group bboxes of buildPreviewGeometry, the
    //    wire edge list, and setTarget / ortho projection math (headless —
    //    GeometryPreview without init() keeps the CPU camera state).
    {
        const std::string src =
            "b = box(size = (4, 2, 6))\n"
            "mp = mark(b, \"right\", where = dot(@P, (1, 0, 0)) > 0)\n"
            "mf = mark(mp, \"top\", where = dot(@N, (0, 1, 0)) > 0.5, domain = faces)\n"
            "output mf\n";
        pgg::RunParams rp;
        rp.pulls = {"mf"};
        pgg::RunResult r = pgg::run(src, rp, {}, "<smoke-target>");
        check(!r.hasErrors() && r.pulled.size() == 1, "value pull for camera targeting");
        if (r.pulled.size() == 1) {
            PreviewBuildOptions opts;
            const PreviewGeometry pg = buildPreviewGeometry(r.pulled[0].value, opts);
            glm::vec3 gc(0.0f);
            float gr = 0.0f;
            bool bbOk = pg.ok && pg.groupBBoxes.size() == 2;
            if (bbOk) {
                const auto it = pg.groupBBoxes.find("points:right");
                bbOk = it != pg.groupBBoxes.end() && pg.groupBBoxes.count("faces:top") == 1;
                if (bbOk) {
                    // Group box inside the scene box (eps) and strictly smaller.
                    const glm::vec3& mn = it->second.first;
                    const glm::vec3& mx = it->second.second;
                    const glm::vec3 eps(1e-4f);
                    bbOk = glm::all(glm::greaterThan(mn, pg.bmin - eps)) &&
                           glm::all(glm::lessThan(mx, pg.bmax + eps)) &&
                           glm::length(mx - mn) < glm::length(pg.bmax - pg.bmin);
                    gc = (mn + mx) * 0.5f;
                    gr = glm::length(mx - mn) * 0.5f;
                }
            }
            check(bbOk, "per-group bboxes (points:right, faces:top) inside the scene bbox");

            // box: 8 shared points, 12 unique undirected edges -> 24 line indices.
            check(pg.wirePositions && pg.wirePositions->size() == 8 && pg.wireIndices.size() == 24,
                  "wire edge list of the box (12 edges, deduplicated)");

            GeometryPreview preview;  // headless: init() never ran, m_ok = false
            preview.setGeometry(pg, true);
            const glm::vec3 sceneCenter = preview.center();
            preview.setTarget(gc, gr);
            check(preview.center() == gc && preview.fitRadius() == gr && preview.center() != sceneCenter,
                  "setTarget moves the orbit center to the group bbox");
            const glm::mat4 persp = preview.viewProj(1.0f);
            preview.setProjection(PreviewProjection::OrthoTop);
            const glm::mat4 ortho = preview.viewProj(1.0f);
            bool finite = persp != ortho;
            for (int c = 0; c < 4; ++c)
                for (int d = 0; d < 4; ++d) finite = finite && std::isfinite(ortho[c][d]);
            check(finite, "ortho top projection: finite matrix, differs from perspective");

            // F2 camera contract: zoom is the fit-distance multiplier
            // (distance = radius * 2.6 * zoom), distance is meters from the
            // orbit center and survives refits as the equivalent fit-zoom.
            preview.setProjection(PreviewProjection::Perspective);
            preview.setZoom(0.5f);
            const bool zoomOk = std::abs(preview.distance() - preview.fitRadius() * 2.6f * 0.5f) < 1e-3f;
            preview.setZoom(2.0f);
            const bool zoom2Ok = std::abs(preview.distance() - preview.fitRadius() * 2.6f * 2.0f) < 1e-3f;
            check(zoomOk && zoom2Ok, "setZoom scales the fit distance (0.5 twice closer, 2 twice farther)");
            preview.setDistance(42.0f);
            check(std::abs(preview.distance() - 42.0f) < 0.5f, "setDistance sets the absolute distance (meters)");
        }
    }

    // 10. Screenshot crop (F1): cropShotPixels on a synthetic 4x3 RGBA buffer
    //     with known pixels — exact content of the rect, clamping of a rect
    //     sticking out of the buffer, rejection of empty intersections.
    {
        const int W = 4, H = 3;
        std::vector<std::uint8_t> buf(static_cast<size_t>(W) * H * 4);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                std::uint8_t* px = buf.data() + (static_cast<size_t>(y) * W + x) * 4;
                px[0] = static_cast<std::uint8_t>(10 * x);  // R = x
                px[1] = static_cast<std::uint8_t>(40 * y);  // G = y
                px[2] = static_cast<std::uint8_t>(x + y);
                px[3] = 255;
            }
        std::vector<std::uint8_t> out;
        int ow = 0, oh = 0;
        bool ok = cropShotPixels(buf, W, H, 1, 1, 2, 2, out, ow, oh);
        bool content = ok && ow == 2 && oh == 2 && out.size() == 16;
        if (content) {
            // Expected top-down rows: (1,1) (2,1) / (1,2) (2,2).
            const std::uint8_t* p = out.data();
            content = p[0] == 10 && p[1] == 40 && p[2] == 2 && p[4] == 20 && p[5] == 40 && p[6] == 3 &&
                      p[8] == 10 && p[9] == 80 && p[10] == 3 && p[12] == 20 && p[13] == 80 && p[14] == 4;
        }
        check(content, "cropShotPixels extracts the rect (top-down rows, exact pixels)");
        // A rect sticking out on the top-left clamps to the buffer: (-1,-1)+3x3
        // over 4x3 -> the 2x2 corner pixels.
        ok = cropShotPixels(buf, W, H, -1, -1, 3, 3, out, ow, oh);
        bool clamped = ok && ow == 2 && oh == 2;
        if (clamped) {
            const std::uint8_t* p = out.data();
            clamped = p[0] == 0 && p[1] == 0 && p[4] == 10 && p[5] == 0 && p[8] == 0 && p[9] == 40 &&
                      p[12] == 10 && p[13] == 40;
        }
        check(clamped, "cropShotPixels clamps a rect sticking out of the buffer");
        check(!cropShotPixels(buf, W, H, 10, 10, 2, 2, out, ow, oh) &&
                  !cropShotPixels(buf, W, H, 1, 1, 0, 2, out, ow, oh),
              "cropShotPixels rejects off-buffer and zero-size rects");
    }

    // 11. Frame compare + silhouettes (F3, CPU): compareFrames on synthetic
    //     buffers with known changed pixels (exact changedPct/bbox, threshold
    //     edges, diff mask content), silhouetteMetrics on a rectangle over a
    //     known background (exact bbox_frac/w_over_h/rows), estimateBackground
    //     corner majority, composeSideBySide geometry.
    {
        const int W = 8, H = 6;
        const std::uint8_t BG[3] = {30, 40, 50};
        // Buffer of bg with an inclusive rect [x0..x1, y0..y1] painted fg.
        auto makeBuf = [&](int x0, int y0, int x1, int y1, std::uint8_t fr, std::uint8_t fg,
                           std::uint8_t fb, int delta = 0) {
            std::vector<std::uint8_t> buf(static_cast<size_t>(W) * H * 4);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    std::uint8_t* px = buf.data() + (static_cast<size_t>(y) * W + x) * 4;
                    const bool inside = x >= x0 && x <= x1 && y >= y0 && y <= y1;
                    px[0] = static_cast<std::uint8_t>((inside ? fr : BG[0]) + delta);
                    px[1] = static_cast<std::uint8_t>((inside ? fg : BG[1]) + delta);
                    px[2] = static_cast<std::uint8_t>((inside ? fb : BG[2]) + delta);
                    px[3] = 255;
                }
            return buf;
        };
        const std::vector<std::uint8_t> a = makeBuf(-1, -1, -1, -1, 0, 0, 0);  // pure bg
        std::vector<std::uint8_t> b = a;
        // A 2x2 block at (3,2)-(4,3) changed well past the threshold.
        for (int y = 2; y <= 3; ++y)
            for (int x = 3; x <= 4; ++x) {
                std::uint8_t* px = b.data() + (static_cast<size_t>(y) * W + x) * 4;
                px[0] = 200;
                px[1] = 100;
                px[2] = 90;
            }
        const FrameCompareResult d = compareFrames(a, b, W, H);
        bool cmpOk = d.available && d.changedPct == 100.0 * 4.0 / 48.0 && d.changeX0 == 3 &&
                     d.changeY0 == 2 && d.changeX1 == 5 && d.changeY1 == 4 &&
                     d.diffPixels.size() == static_cast<size_t>(W) * H * 4;
        if (cmpOk) {
            // Changed pixels are magenta; unchanged are the dimmed new frame.
            const std::uint8_t* m = d.diffPixels.data() + (static_cast<size_t>(2) * W + 3) * 4;
            const std::uint8_t* u = d.diffPixels.data();
            cmpOk = m[0] == 255 && m[1] == 0 && m[2] == 255 && m[3] == 255 && u[0] == 9 &&
                    u[1] == 12 && u[2] == 15 && u[3] == 255;
        }
        check(cmpOk, "compareFrames: exact changedPct/bbox, magenta mask over the dimmed frame");
        const FrameCompareResult same = compareFrames(a, a, W, H);
        check(same.available && same.changedPct == 0.0 && same.changeX0 == 0 && same.changeX1 == 0,
              "compareFrames: identical frames -> 0%, empty bbox");
        // Threshold edges: a delta of exactly kFrameChangeThreshold on every
        // channel is NOT a change, threshold+1 is.
        const FrameCompareResult below = compareFrames(a, makeBuf(-1, -1, -1, -1, 0, 0, 0, 8), W, H);
        const FrameCompareResult above = compareFrames(a, makeBuf(-1, -1, -1, -1, 0, 0, 0, 9), W, H);
        check(below.available && below.changedPct == 0.0 && above.available && above.changedPct == 100.0,
              "compareFrames: per-channel threshold (8 no, 9 yes)");
        const FrameCompareResult badSize =
            compareFrames(a, std::vector<std::uint8_t>{}, W, H);
        check(!badSize.available && !badSize.reason.empty(), "compareFrames: size mismatch -> unavailable");

        const std::vector<std::uint8_t> rect = makeBuf(2, 1, 5, 4, 220, 220, 220);
        const SilhouetteMetrics sm = silhouetteMetrics(rect, W, H, BG[0], BG[1], BG[2]);
        bool silOk = !sm.empty && sm.bboxX0 == 2.0f / 8.0f && sm.bboxY0 == 1.0f / 6.0f &&
                     sm.bboxX1 == 6.0f / 8.0f && sm.bboxY1 == 5.0f / 6.0f && sm.wOverH == 1.0f;
        if (silOk) {
            // Bands (y*10/6): the rect's rows 1..4 land in bands 1, 3, 5, 6 —
            // each one row tall -> 4 fg of 8 px = 0.5; all other bands 0.
            silOk = sm.rows[0] == 0.0f && sm.rows[1] == 0.5f && sm.rows[2] == 0.0f &&
                    sm.rows[3] == 0.5f && sm.rows[4] == 0.0f && sm.rows[5] == 0.5f &&
                    sm.rows[6] == 0.5f && sm.rows[7] == 0.0f && sm.rows[8] == 0.0f &&
                    sm.rows[9] == 0.0f;
        }
        check(silOk, "silhouetteMetrics: exact bbox_frac/w_over_h/band rows of a rect");
        check(silhouetteMetrics(a, W, H, BG[0], BG[1], BG[2]).empty,
              "silhouetteMetrics: pure background -> empty");
        // Tolerance edges: bg+8 per channel stays background, bg+9 is fg.
        check(silhouetteMetrics(makeBuf(-1, -1, -1, -1, 0, 0, 0, 8), W, H, BG[0], BG[1], BG[2]).empty &&
                  !silhouetteMetrics(makeBuf(-1, -1, -1, -1, 0, 0, 0, 9), W, H, BG[0], BG[1], BG[2]).empty,
              "silhouetteMetrics: background tolerance (8 no, 9 yes)");

        // estimateBackground: 3 corners A + 1 corner B -> A; a 2-2 tie goes to
        // the first corner in TL,TR,BL,BR order.
        {
            std::vector<std::uint8_t> corners = a;  // all four corners are BG
            std::uint8_t* br = corners.data() + (static_cast<size_t>(H - 1) * W + (W - 1)) * 4;
            br[0] = 200;
            br[1] = 210;
            br[2] = 220;
            const auto maj = estimateBackground(corners, W, H);
            check(maj[0] == BG[0] && maj[1] == BG[1] && maj[2] == BG[2],
                  "estimateBackground: corner majority wins");
            // TL=TR=(7,8,9), BL=BR=(200,210,220): a 2-2 tie -> TL's color.
            std::vector<std::uint8_t> tie = corners;
            for (std::uint8_t* px : {tie.data(), tie.data() + static_cast<size_t>(W - 1) * 4}) {
                px[0] = 7;
                px[1] = 8;
                px[2] = 9;
            }
            const auto majTie = estimateBackground(tie, W, H);
            check(majTie[0] == 7 && majTie[1] == 8 && majTie[2] == 9,
                  "estimateBackground: a 2-2 tie resolves to the first corner (TL)");
        }

        // composeSideBySide: ref 4x4 -> resized to the model height 6 keeping
        // aspect (6 px wide); total width = 8 + 4 divider + 6 = 18.
        const std::vector<std::uint8_t> ref(static_cast<size_t>(4) * 4 * 4, 0);
        std::vector<std::uint8_t> refC = ref;
        for (size_t i = 0; i + 3 < refC.size(); i += 4) {
            refC[i] = 10;
            refC[i + 1] = 200;
            refC[i + 2] = 60;
            refC[i + 3] = 255;
        }
        const SideBySideImage sbs = composeSideBySide(rect, W, H, refC, 4, 4);
        bool sbsOk = sbs.ok && sbs.width == 18 && sbs.height == 6 && sbs.modelW == 8 &&
                     sbs.pixels.size() == static_cast<size_t>(18) * 6 * 4;
        if (sbsOk) {
            // Left half keeps the model pixels, the divider is grey 90, the
            // uniform reference stays its color after the resize.
            const std::uint8_t* modelPx = sbs.pixels.data() + (static_cast<size_t>(1) * 18 + 2) * 4;
            const std::uint8_t* divPx = sbs.pixels.data() + (static_cast<size_t>(1) * 18 + 8) * 4;
            const std::uint8_t* refPx = sbs.pixels.data() + (static_cast<size_t>(1) * 18 + 13) * 4;
            sbsOk = modelPx[0] == 220 && divPx[0] == 90 && divPx[1] == 90 && divPx[2] == 90 &&
                    refPx[0] == 10 && refPx[1] == 200 && refPx[2] == 60;
        }
        check(sbsOk, "composeSideBySide: size/aspect, divider, model and ref halves");
        check(!composeSideBySide(rect, W, H, refC, 0, 4).ok &&
                  !composeSideBySide(std::vector<std::uint8_t>{}, W, H, refC, 4, 4).ok,
              "composeSideBySide: rejects bad inputs");
    }

    // 12. --serve RPC: a real server plus an in-process socket client, driven
    //    by a manual poll() loop (headless: no sokol, no frame loop — render
    //    must fail with no_frame_loop).
    if (!serveAddress.empty()) {
        std::string host = "127.0.0.1";
        uint16_t port = ViewerRpcServer::kDefaultPort;
        const size_t colon = serveAddress.rfind(':');
        if (colon != std::string::npos) {
            host = serveAddress.substr(0, colon);
            if (host.empty()) host = "127.0.0.1";
            port = static_cast<uint16_t>(std::atoi(serveAddress.substr(colon + 1).c_str()));
        } else if (serveAddress.find_first_not_of("0123456789") == std::string::npos) {
            port = static_cast<uint16_t>(std::atoi(serveAddress.c_str()));
        } else {
            host = serveAddress;
        }

        ViewerRpcServer server;
        registerPggViewerRpcHandlers(server);
        check(server.start(host, port), "rpc server starts on the --serve address");

        // Blocking client socket with a short recv timeout; every request is
        // answered after a few manual server.poll() calls.
        bool clientOk = false;
        std::string inbuf;
#if defined(_WIN32)
        WSADATA wsa = {};
        SOCKET cfd = INVALID_SOCKET;
        if (WSAStartup(MAKEWORD(2, 2), &wsa) == 0) cfd = socket(AF_INET, SOCK_STREAM, 0);
        const DWORD recvTimeoutMs = 50;
        const bool socketOk = cfd != INVALID_SOCKET;
#else
        int cfd = socket(AF_INET, SOCK_STREAM, 0);
        const timeval recvTimeout{0, 50 * 1000};
        const bool socketOk = cfd >= 0;
#endif
        if (socketOk) {
            sockaddr_in addr = {};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(port);
            inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
#if defined(_WIN32)
            setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&recvTimeoutMs),
                       sizeof(recvTimeoutMs));
#else
            setsockopt(cfd, SOL_SOCKET, SO_RCVTIMEO, &recvTimeout, sizeof(recvTimeout));
#if defined(SO_NOSIGPIPE)
            const int one = 1;
            setsockopt(cfd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
#endif
            clientOk = connect(cfd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
        }
        check(clientOk, "rpc client connects");

        auto call = [&](const nlohmann::json& req) -> nlohmann::json {
            const std::string line = req.dump() + "\n";
            if (send(cfd, line.data(), line.size(), 0) < 0) return nlohmann::json{};
            for (int iter = 0; iter < 1200; ++iter) {  // bounded: ~60 s worst case
                server.poll();
                char buf[65536];
#if defined(_WIN32)
                const int n = recv(cfd, buf, sizeof(buf), 0);
#else
                const ssize_t n = recv(cfd, buf, sizeof(buf), 0);
#endif
                if (n > 0) {
                    inbuf.append(buf, static_cast<size_t>(n));
                    const size_t nl = inbuf.find('\n');
                    if (nl != std::string::npos) {
                        const std::string resp = inbuf.substr(0, nl);
                        inbuf.erase(0, nl + 1);
                        return nlohmann::json::parse(resp, nullptr, false);
                    }
                } else if (n == 0) {
                    break;  // server closed
                }
            }
            return nlohmann::json{};
        };

        if (clientOk) {
            const nlohmann::json pong = call({{"op", "ping"}, {"args", nlohmann::json::object()}});
            check(pong.value("ok", false) && pong["data"].value("pong", false), "rpc ping -> pong");

            const nlohmann::json status = call({{"op", "status"}, {"args", nlohmann::json::object()}});
            check(status.value("ok", false) && status["data"].contains("cache") &&
                      status["data"].contains("preview"),
                  "rpc status");

            const nlohmann::json bad =
                call({{"op", "load"}, {"args", {{"source", "= definitely not pgg (\n"}}}});
            check(bad.value("ok", false) && bad["data"].value("has_errors", false) &&
                      !bad["data"]["diagnostics"].empty(),
                  "rpc load of an invalid source answers diagnostics without a run");

            // F5: the static schema reaches through a def call in the source
            // of instance_on_points (param-driven group name, parts.piece
            // style) — the lamp_fence E609 incident is answered by load in
            // milliseconds, not by a run.
            const nlohmann::json e609 = call(
                {{"op", "load"},
                 {"args",
                  {{"source",
                    "def part(grp: string) -> (out: geo<mesh>) {\n"
                    "    out = mark(box(size = vec3(1.0)), grp, where = true, domain = faces)\n"
                    "}\n"
                    "pts = mesh_line(count = 3, length = 2.0, dir = (1, 0, 0))\n"
                    "bars = realize(instance_on_points(pts, source = part(grp = \"iron\")))\n"
                    "pier = set(box(size = vec3(2.0)), \"tint\", vec3(0.5, 0.5, 0.5), domain = faces)\n"
                    "scene = merge(pier, bars)\n"
                    "output scene\n"}}}});
            bool e609seen = false;
            if (e609.value("ok", false) && e609["data"].contains("diagnostics"))
                for (const nlohmann::json& d : e609["data"]["diagnostics"])
                    e609seen = e609seen || d.value("code", std::string{}) == "E609";
            check(e609seen && e609["data"].value("has_errors", false) &&
                      e609["data"].value("ms", 1000.0) < 1000.0,
                  "rpc load catches E609 through a def instance source (static, ms-budget)");

            const nlohmann::json good =
                call({{"op", "load"}, {"args", {{"path", corpus + "/e1_rock.pgg"}}}});
            check(good.value("ok", false) && !good["data"].value("has_errors", true),
                  "rpc load of a corpus file");

            const nlohmann::json prm =
                call({{"op", "params"}, {"args", {{"seed", 7}}}});
            check(prm.value("ok", false) && prm["data"]["params"].value("seed", std::string{}) == "7",
                  "rpc params sets a launch param");

            const nlohmann::json probe =
                call({{"op", "probe"}, {"args", {{"spec", "base:schema"}}}});
            bool probeOk = probe.value("ok", false) && !probe["data"]["records"].empty();
            if (probeOk)
                probeOk = probe["data"]["records"][0]["text"].get<std::string>().find("mesh") !=
                          std::string::npos;
            check(probeOk, "rpc probe base:schema returns a record");

            const nlohmann::json render =
                call({{"op", "render"}, {"args", {{"node", "base"}}}});
            check(!render.value("ok", true) && render["error"].value("kind", std::string{}) ==
                      "no_frame_loop",
                  "rpc render fails headless with no_frame_loop");

            // D3: builtin docs — file-independent registry card.
            const nlohmann::json bdoc =
                call({{"op", "docs"}, {"args", {{"symbol", "builtin:clip"}}}});
            check(bdoc.value("ok", false) &&
                      bdoc["data"].value("kind", std::string{}) == "builtin" &&
                      bdoc["data"].value("signature", std::string{}).rfind("clip(", 0) == 0 &&
                      bdoc["data"].value("group", std::string{}) == "topology" &&
                      !bdoc["data"].value("summary", std::string{}).empty() &&
                      !bdoc["data"].value("example", std::string{}).empty(),
                  "rpc docs builtin:clip returns the registry signature + card");
            const nlohmann::json bdoc404 =
                call({{"op", "docs"}, {"args", {{"symbol", "builtin:nope"}}}});
            check(!bdoc404.value("ok", true) &&
                      bdoc404["error"].value("kind", std::string{}) == "not_found",
                  "rpc docs builtin:nope -> not_found");
            // The def path still answers over the loaded file (kind = "def"
            // since D3); a binding name is not a def.
            const nlohmann::json ddoc =
                call({{"op", "docs"}, {"args", {{"symbol", "base"}}}});
            check(!ddoc.value("ok", true) &&
                      ddoc["error"].value("kind", std::string{}) == "not_found",
                  "rpc docs of a non-def symbol -> not_found");

            // F1/F2: the frame-loop gate fires before the new args are even
            // parsed — headless the answer must stay no_frame_loop.
            const nlohmann::json renderF1 =
                call({{"op", "render"},
                      {"args", {{"node", "base"}, {"frame", "preview"}, {"chrome", "off"},
                                {"zoom", 0.5}, {"distance", 30}, {"size", {640, 480}}}}});
            check(!renderF1.value("ok", true) &&
                      renderF1["error"].value("kind", std::string{}) == "no_frame_loop",
                  "rpc render with frame/chrome/zoom/distance fails headless with no_frame_loop");

            // F3: compare and reference sit behind the same frame-loop gate —
            // headless the answer must stay no_frame_loop (before arg parsing,
            // so the reference image does not need to exist).
            const nlohmann::json renderCmp =
                call({{"op", "render"}, {"args", {{"node", "base"}, {"compare", "prev"}}}});
            check(!renderCmp.value("ok", true) &&
                      renderCmp["error"].value("kind", std::string{}) == "no_frame_loop",
                  "rpc render compare=prev fails headless with no_frame_loop");
            const nlohmann::json refHeadless = call(
                {{"op", "reference"}, {"args", {{"node", "base"}, {"image", "tmp/nope.png"}}}});
            check(!refHeadless.value("ok", true) &&
                      refHeadless["error"].value("kind", std::string{}) == "no_frame_loop",
                  "rpc reference fails headless with no_frame_loop");

            // F4 auto-reload by mtime: the smoke owns a scratch file, rewrites
            // it between calls and expects the next run command to reload it
            // by itself (no explicit load). render needs the frame loop —
            // headless it fails before the reload — so probe/export carry the
            // check. The mtime is pinned forward on every write: a back-to-back
            // rewrite could otherwise tie the recorded timestamp.
            {
                namespace fs = std::filesystem;
                const fs::path scratch = fs::path(findRepoRoot()) / "tmp" / "pgg_smoke_reload.pgg";
                const std::string scratchObj = (fs::path(findRepoRoot()) / "tmp" / "pgg_smoke_reload.obj").string();
                std::error_code ec;
                fs::create_directories(scratch.parent_path(), ec);
                const std::string srcA =
                    "param size: int = 1\n"
                    "base = ico_sphere(subdiv = size, radius = 1.0)\n"
                    "output base\n";
                const std::string srcB =
                    "param radius: int = 2\n"
                    "orb = ico_sphere(subdiv = 2, radius = radius)\n"
                    "output orb\n";
                int bump = 0;
                auto writeScratch = [&](const std::string& text) {
                    {
                        std::ofstream out(scratch, std::ios::binary | std::ios::trunc);
                        out << text;
                    }
                    fs::last_write_time(scratch,
                                        fs::file_time_type::clock::now() + std::chrono::seconds(++bump), ec);
                };

                writeScratch(srcA);
                const nlohmann::json ld =
                    call({{"op", "load"}, {"args", {{"path", scratch.string()}}}});
                check(ld.value("ok", false) && !ld["data"].value("has_errors", true),
                      "f4: load of the scratch file");

                const nlohmann::json p1 = call({{"op", "probe"}, {"args", {{"spec", "base:schema"}}}});
                check(p1.value("ok", false) && !p1["data"].value("reloaded", true) &&
                          !p1["data"]["records"].empty(),
                      "f4: probe right after load answers reloaded:false");

                writeScratch(srcB);
                const nlohmann::json p2 = call({{"op", "probe"}, {"args", {{"spec", "orb:schema"}}}});
                bool f4reload = p2.value("ok", false) && p2["data"].value("reloaded", false) &&
                                p2["data"].contains("load_diagnostics") &&
                                p2["data"]["load_diagnostics"].empty() &&
                                !p2["data"]["records"].empty();
                if (f4reload)
                    f4reload = p2["data"]["records"][0]["text"].get<std::string>().find("mesh") !=
                               std::string::npos;
                check(f4reload,
                      "f4: on-disk edit -> next probe reloads (reloaded:true, clean load_diagnostics)");
                const nlohmann::json st = call({{"op", "status"}, {"args", nlohmann::json::object()}});
                check(st.value("ok", false) && st["data"]["params"].contains("radius") &&
                          !st["data"]["params"].contains("size"),
                      "f4: the reload refreshed the viewer state (param set of the new file)");
                // E: status carries the last run's per-binding profile
                // (top-20 rows: name/ms/field_evals/cache_hit) + the total.
                bool profOk = st["data"].contains("profile") && st["data"].contains("profile_total_ms") &&
                              !st["data"]["profile"].empty();
                if (profOk) {
                    profOk = false;
                    for (const auto& row : st["data"]["profile"])
                        if (row.value("name", std::string{}) == "orb" && row.value("ms", -1.0) >= 0.0 &&
                            row.contains("field_evals") && row.contains("cache_hit"))
                            profOk = true;
                }
                check(profOk, "e: status carries the last run's profile (row for the pulled binding)");

                const nlohmann::json p3 = call({{"op", "probe"}, {"args", {{"spec", "orb:schema"}}}});
                check(p3.value("ok", false) && !p3["data"].value("reloaded", true),
                      "f4: unchanged file -> reloaded:false again");

                const nlohmann::json e1 =
                    call({{"op", "export"}, {"args", {{"node", "orb"}, {"obj_path", scratchObj}}}});
                check(e1.value("ok", false) && !e1["data"].value("reloaded", true),
                      "f4: export without edits answers reloaded:false");
                writeScratch(srcA);
                const nlohmann::json e2 =
                    call({{"op", "export"}, {"args", {{"node", "base"}, {"obj_path", scratchObj}}}});
                check(e2.value("ok", false) && e2["data"].value("reloaded", false) &&
                          e2["data"]["stats"].value("pts", 0) == 42,
                      "f4: on-disk edit -> next export reloads (reloaded:true, ico_sphere subdiv 1 = 42 pts)");

                writeScratch("= definitely not pgg (\n");
                const nlohmann::json p4 = call({{"op", "probe"}, {"args", {{"spec", "base:schema"}}}});
                check(!p4.value("ok", true) &&
                          p4["error"].value("kind", std::string{}) == "run_errors",
                      "f4: broken edit -> run_errors without a run");

                writeScratch(srcA);
                const nlohmann::json p5 = call({{"op", "probe"}, {"args", {{"spec", "base:schema"}}}});
                check(p5.value("ok", false) && p5["data"].value("reloaded", false),
                      "f4: fixed file -> reload recovers");
            }

            // RPC diff (C2, server side): the outputs-fingerprint snapshot —
            // first call records the baseline, an unchanged file diffs
            // identical, an on-disk edit is caught by the in-diff auto-reload
            // and reports changed, and only update:true refreshes the snapshot.
            {
                namespace fs = std::filesystem;
                const fs::path scratch = fs::path(findRepoRoot()) / "tmp" / "pgg_smoke_diff.pgg";
                std::error_code ec;
                fs::create_directories(scratch.parent_path(), ec);
                const std::string srcC =
                    "base = ico_sphere(subdiv = 1, radius = 1.0)\n"
                    "output base\n";
                const std::string srcC2 =
                    "base = ico_sphere(subdiv = 2, radius = 1.0)\n"
                    "output base\n";
                const std::string srcD =
                    "orb = ico_sphere(subdiv = 1, radius = 1.0)\n"
                    "output orb\n";
                int bump = 0;
                auto writeScratch = [&](const std::string& text) {
                    {
                        std::ofstream out(scratch, std::ios::binary | std::ios::trunc);
                        out << text;
                    }
                    // Pin the mtime forward (well past the f4 block's pins): a
                    // back-to-back rewrite could otherwise tie the recorded
                    // timestamp and the auto-reload would not see the edit.
                    fs::last_write_time(scratch,
                                        fs::file_time_type::clock::now() + std::chrono::seconds(1000 + ++bump),
                                        ec);
                };

                writeScratch(srcC);
                const nlohmann::json ld =
                    call({{"op", "load"}, {"args", {{"path", scratch.string()}}}});
                check(ld.value("ok", false) && !ld["data"].value("has_errors", true),
                      "diff: load of the scratch file");

                const nlohmann::json d1 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
                check(d1.value("ok", false) && d1["data"].value("baseline_created", false) &&
                          !d1["data"]["outputs"].empty(),
                      "diff: first call records the baseline");

                const nlohmann::json d2 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
                check(d2.value("ok", false) && !d2["data"].value("baseline_created", true) &&
                          d2["data"].value("identical", false) &&
                          d2["data"]["outputs"][0].value("status", std::string{}) == "identical",
                      "diff: unchanged file is identical");

                writeScratch(srcC2);
                const nlohmann::json d3 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
                check(d3.value("ok", false) && d3["data"].value("reloaded", false) &&
                          !d3["data"].value("identical", true) &&
                          d3["data"]["outputs"][0].value("status", std::string{}) == "changed" &&
                          d3["data"]["outputs"][0].value("fingerprint_prev", std::string{}) !=
                              d3["data"]["outputs"][0].value("fingerprint_now", std::string{}),
                      "diff: on-disk edit -> changed (auto-reload inside diff)");

                // Without update the snapshot still holds the old fingerprint.
                const nlohmann::json d4 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
                check(d4.value("ok", false) && !d4["data"].value("identical", true),
                      "diff: the snapshot is kept without update:true");
                const nlohmann::json d5 = call({{"op", "diff"}, {"args", {{"update", true}}}});
                check(d5.value("ok", false) && d5["data"].value("snapshot_updated", false),
                      "diff: update:true refreshes the snapshot");
                const nlohmann::json d6 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
                check(d6.value("ok", false) && d6["data"].value("identical", false),
                      "diff: identical after the update");

                // A renamed output reports added + removed (not identical).
                writeScratch(srcD);
                const nlohmann::json d7 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
                bool addedRemoved = d7.value("ok", false) && !d7["data"].value("identical", true);
                bool sawAdded = false, sawRemoved = false;
                if (addedRemoved)
                    for (const auto& o : d7["data"]["outputs"]) {
                        sawAdded = sawAdded || o.value("status", std::string{}) == "added";
                        sawRemoved = sawRemoved || o.value("status", std::string{}) == "removed";
                    }
                check(addedRemoved && sawAdded && sawRemoved,
                      "diff: renamed output -> added + removed");

                // load{snapshot:true} records a fresh baseline for the new state.
                const nlohmann::json ld2 = call(
                    {{"op", "load"}, {"args", {{"path", scratch.string()}, {"snapshot", true}}}});
                check(ld2.value("ok", false) && ld2["data"].value("snapshot", false),
                      "diff: load with snapshot:true records the baseline");
                const nlohmann::json d8 = call({{"op", "diff"}, {"args", nlohmann::json::object()}});
                check(d8.value("ok", false) && !d8["data"].value("baseline_created", true) &&
                          d8["data"].value("identical", false),
                      "diff: identical against the load-time snapshot");
            }
        }
        if (clientOk) {
#if defined(_WIN32)
            closesocket(cfd);
            WSACleanup();
#else
            close(cfd);
#endif
        }
        server.stop();
    }

    if (g_failures == 0) {
        spdlog::info("TEST PASS: PggViewer smoke (all checks)");
    } else {
        spdlog::error("TEST FAIL: PggViewer smoke, {} check(s) failed", g_failures);
    }
    return g_failures == 0;
}
