#include "pch.h"

#include "SmokeTest.h"

#include <cmath>
#include <filesystem>

#include <spdlog/spdlog.h>

#include <pgg/eval.h>
#include <pgg/pgg.h>
#include <pgg/src/eval/expand.h>
#include <pgg/src/eval/modules.h>
#include <pgg/src/graph.h>
#include <pgg/src/layout.h>

#include "GeometryPreview.h"

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

bool runPggViewerSmokeTest() {
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

    if (g_failures == 0) {
        spdlog::info("TEST PASS: PggViewer smoke (all checks)");
    } else {
        spdlog::error("TEST FAIL: PggViewer smoke, {} check(s) failed", g_failures);
    }
    return g_failures == 0;
}
