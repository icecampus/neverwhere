// PggViewer: read-only node-graph projection of .pgg files (spec §10, stage E8).
//   PggViewer [file.pgg] [--shot=out.png] [--shot-delay=S] [--zoom=Z] [--center=X,Y] [--no-ui]
//             [--dive=<ipath>] [--preview=<pull path>] [--preview-highlight=<domain>:<group>]
//             [--preview-shading=auto|smooth|flat] [--preview-colors=on|off] [--preview-size=W,H]
//             [--preview-orbit=yaw_deg,pitch_deg[,zoom]]
//             [--preview-target=x,y,z|group:<name>|binding:<path>] [--preview-fit=all|target]
//             [--preview-ortho=front|side|top] [--preview-wire=on] [--param=name=value]...
//             [--serve[=host:port]]
//   PggViewer --smoke
// The graph is derived from the text (no separate storage): names are nodes,
// uses are wires, def calls collapse into diveable nodes addressed by their
// instance path, repeat/foreach zones draw as subgraphs with iteration ports
// and a state loop. Layout hints live in trailing `# @pos X Y` comments and
// are written back on node drags (in memory; Save persists). The probe panel
// reuses the E6 probe API (PggTool --probe). The right region is a split
// view: graph canvas on top, preview pane below, draggable splitter (default
// 1:2). The preview renders the value of the selected node
// (RunParams::pulls -> GeometryPreview): meshes and points directly,
// instances realized, sdf meshed at a preview voxel.
// --serve (agent tooling plan A1, docs/pgg/agent_tooling_plan.md): TCP RPC
// server (ViewerRpcServer) polled from frame(); a session MemoryCache warms
// repeated runs; --shot with --preview and no explicit --shot-delay fires on
// the first committed frame after the run instead of the wall-time delay.

#include "pch.h"

#include <cmath>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>

#include <pgg/eval.h>
#include <pgg/pgg.h>
#include <pgg/src/eval/builtins.h>  // realizeInstances for the RPC export
#include <pgg/src/eval/cache.h>
#include <pgg/src/eval/docs_lookup.h>
#include <pgg/src/eval/expand.h>
#include <pgg/src/eval/modules.h>
#include <pgg/src/eval/obj_export.h>
#include <pgg/src/eval/sdf.h>
#include <pgg/src/eval/typecheck.h>
#include <pgg/src/graph.h>
#include <pgg/src/layout.h>

#include "FileDialog.h"
#include "GeometryPreview.h"
#include "GraphCanvas.h"
#include "SmokeTest.h"
#include "ViewerRpcServer.h"

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #elif defined(__APPLE__)
        #define SOKOL_METAL
    #elif defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE
    #endif
#endif

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <sokol_time.h>
#include <util/sokol_imgui.h>

// Xlib.h (via sokol_app.h on Linux) defines None as a macro (0L); it collides
// with CameraTargetSpec::Kind::None below. This TU never calls Xlib directly.
#if defined(None)
    #undef None
#endif

#if defined(SOKOL_METAL) && defined(__APPLE__)
    #import <Foundation/Foundation.h>
    #import <Metal/Metal.h>
    #import <QuartzCore/CAMetalLayer.h>
    #import <dispatch/dispatch.h>
#elif defined(SOKOL_D3D11)
    #include <d3d11.h>
#endif

#if !defined(_WIN32)
    #define STB_IMAGE_WRITE_IMPLEMENTATION
#endif
#include <stb_image_write.h>

namespace {

struct AppState {
    uint64_t lastTime = 0;
    float dt = 1.0f / 60.0f;
    bool gfxOk = false;
    bool imguiOk = false;
};

AppState g_state;

// --- document state ---------------------------------------------------------------

std::string g_filePath;
std::string g_text;  // in-memory source (hint write-backs land here; Save persists)
pgg::Document g_doc;
std::unique_ptr<pgg::ModuleClosure> g_closure;
std::vector<pgg::Diagnostic> g_allDiags;  // parse/lint + import closure
pgg::GraphProject g_project;
pgg::LayoutParams g_layout;
bool g_dirty = false;
std::vector<std::pair<std::string, std::string>> g_paramValues;  // param name -> field text

// --- navigation / panels ------------------------------------------------------------

std::vector<std::string> g_dive;  // full instance paths ("" level = top scope)
GraphCanvasState g_canvas;
bool g_needFitView = false;
std::string g_probeText;
char g_pathBuf[1024] = {0};
FileDialogState g_fileDialog;

// --- geometry preview -----------------------------------------------------------

GeometryPreview g_preview;
bool g_showPreview = true;
float g_splitRatio = 1.0f / 3.0f;  // graph : preview height share (default 1:2)
bool g_autoPreview = true;           // re-run the preview when the selection changes
std::string g_previewTarget;         // pull path currently shown
pgg::Value g_previewValue;           // last pulled value (rebuilt on highlight/resolution changes)
bool g_previewHasValue = false;
PreviewBuildOptions g_previewOpts;
std::vector<std::string> g_previewGroups;
bool g_previewHasColor = false;      // the last build found a vec3 @Cd
int g_previewLastSelected = -2;      // (selected index, scope) the auto-preview last ran for
std::string g_previewLastScope;
std::string g_cliPreview;            // --preview=<path>: pull + show at startup
ImVec2 g_cliPreviewSize{0.0f, 0.0f};  // --preview-size=W,H: initial preview window size (points)
std::optional<glm::vec3> g_cliOrbit;  // --preview-orbit=yaw,pitch[,zoom]: camera (deg, deg, fit multiplier)
std::vector<std::pair<std::string, std::string>> g_cliParams;  // --param=name=value

// A2 camera targeting: --preview-target=x,y,z|group:<name>|binding:<path>
// (RPC render takes the same syntax as its "target" arg). The spec survives
// refits: it is re-applied after every runPreview rebuild, group targets
// re-resolving against the fresh per-group bboxes of the new geometry.
struct CameraTargetSpec {
    enum class Kind { None, Point, Group, Binding };
    Kind kind = Kind::None;
    glm::vec3 point{0.0f};
    std::string name;  // group name ("<domain>:<name>" or bare) / binding pull path
    std::string raw;   // as given (logs)
};
CameraTargetSpec g_cameraTarget;  // active target ("" = none); RPC "" clears it
std::string g_cliPreviewTarget;
std::string g_cliPreviewFit;    // --preview-fit=all|target ("" = auto: target iff a target is given)
std::string g_cliPreviewOrtho;  // --preview-ortho=front|side|top
bool g_cliPreviewWire = false;  // --preview-wire=on
std::map<std::string, std::pair<glm::vec3, glm::vec3>> g_previewGroupBBoxes;  // of the last build
// binding: target resolution cache: pulling it costs a run (zones are
// uncached by design), so it is resolved once per load, not per rebuild.
bool g_bindingTargetResolved = false;
glm::vec3 g_bindingTargetCenter{0.0f};
float g_bindingTargetRadius = 1.0f;

// --- CLI ----------------------------------------------------------------------------

std::string g_pendingLoad;
std::string g_shotPath;
double g_shotDelaySec = 1.0;  // --shot-delay=: wall time before the capture
bool g_shotDelayExplicit = false;  // --shot-delay given: keep the wall-time behaviour
std::optional<float> g_cliZoom;
std::optional<ImVec2> g_cliCenter;
std::string g_cliDive;
bool g_noUi = false;

// --- RPC server (--serve) + session run cache -----------------------------------------

// Session-wide cross-run cache (A1): keys are structural AST fingerprints, so
// editing the file invalidates only downstream bindings and the instance can
// outlive loadFile/Reload of the same file. Created in init(); absent in
// --smoke (rp.cache = nullptr then, runs are uncached).
std::unique_ptr<pgg::MemoryCache> g_memoryCache;
uint64_t g_lastCacheHits = 0, g_lastCacheMisses = 0;  // counters of the last run
double g_lastRunMs = 0.0;                             // wall time of the last preview/probe run
std::vector<pgg::Diagnostic> g_lastRunDiags;          // diagnostics of the last preview run
std::string g_lastPreviewError;                       // runPreview failure text ("" when ok)

std::string g_serveAddress;  // --serve[=host:port] ("" = off)
std::unique_ptr<ViewerRpcServer> g_rpc;
std::vector<std::string> g_rpcImportRoots;  // lib_roots of the last RPC load{source}
double g_startTimeSec = 0.0;                // wallNowSec() at init (status uptime)
uint64_t g_shotCounter = 0;                 // default shot_N.png numbering
uint64_t g_srcCounter = 0;                  // load{source} temp-file numbering
// Frames committed since runPreview finished (any path): the readiness signal
// for the RPC render reply and for the CLI --shot without --shot-delay.
int g_framesSincePreviewRun = 0;

// Deferred RPC render (phase 2): set by the render handler (poll phase), the
// reply is sent from frame() after the first committed frame carries the new
// geometry and capturePng grabbed it.
struct PendingRender {
    bool active = false;
    uint64_t clientId = 0;
    std::string outPath;
    std::string node;
    nlohmann::json stats;  // {kind,pts,tri,bbox,groups,ms} of the pulled value
    uint64_t cacheHits = 0, cacheMisses = 0;
};
PendingRender g_pendingRender;

constexpr float kPanelWidth = 380.0f;
constexpr float kSplitterHeight = 6.0f;

float panelWidth() { return g_state.imguiOk ? kPanelWidth : 0.0f; }

std::string literalText(const pgg::Expr* e) {
    if (!e) return {};
    switch (e->kind) {
        case pgg::NodeKind::NumberLit: return static_cast<const pgg::NumberLit*>(e)->text;
        case pgg::NodeKind::StringLit: return static_cast<const pgg::StringLit*>(e)->value;
        case pgg::NodeKind::BoolLit:
            return static_cast<const pgg::BoolLit*>(e)->value ? "true" : "false";
        default: return {};
    }
}

bool loadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        spdlog::error("PggViewer: cannot open {}", path);
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();

    // Rebuild order matters: the project holds raw pointers into the
    // document's arena and the closure's module infos.
    g_project = pgg::GraphProject{};
    g_closure.reset();
    g_doc = pgg::Document{};

    g_text = ss.str();
    g_doc = pgg::parse(g_text, path);
    g_filePath = path;
    g_allDiags = g_doc.diagnostics;
    if (g_doc.file && pgg::hasImports(*g_doc.file)) {
        // RPC load{source} carries extra lib_roots; they stay in effect until
        // the next RPC load (like --lib on PggTool).
        std::vector<std::string> roots = g_rpcImportRoots;
        const std::string dir = std::filesystem::path(path).parent_path().string();
        if (!dir.empty()) roots.push_back(dir);
        std::vector<pgg::Diagnostic> diags;
        g_closure = std::make_unique<pgg::ModuleClosure>(pgg::loadModuleClosure(*g_doc.file, roots, diags));
        g_allDiags.insert(g_allDiags.end(), diags.begin(), diags.end());
    }
    g_project = pgg::buildGraph(g_doc, g_closure.get());
    pgg::layoutProject(g_project, g_layout);

    g_dive.clear();
    g_canvas = GraphCanvasState{};
    g_probeText.clear();
    g_preview.clear();
    g_preview.setSummary({});
    g_preview.setError({});
    g_previewTarget.clear();
    g_previewHasValue = false;
    g_previewGroups.clear();
    g_previewGroupBBoxes.clear();
    g_previewLastSelected = -2;
    g_bindingTargetResolved = false;  // binding: camera targets re-resolve on the new file
    g_dirty = false;
    g_needFitView = true;
    g_paramValues.clear();
    if (g_doc.file) {
        for (const pgg::Node* item : g_doc.file->items) {
            if (item->kind != pgg::NodeKind::ParamDecl) continue;
            const auto* p = static_cast<const pgg::ParamDecl*>(item);
            g_paramValues.push_back({p->name, p->hasDefault ? literalText(p->def) : std::string{}});
        }
    }
    // CLI --param overrides (applied on every load, so Reload keeps them).
    for (const auto& [name, text] : g_cliParams)
        for (auto& [pname, ptext] : g_paramValues)
            if (pname == name) ptext = text;
    std::snprintf(g_pathBuf, sizeof(g_pathBuf), "%s", path.c_str());
    spdlog::info("PggViewer: loaded {} ({} nodes, {} instance scopes)", path, g_project.top.nodes.size(),
                 g_project.instanceScopes.size());
    return true;
}

void saveFile() {
    std::ofstream out(g_filePath, std::ios::binary | std::ios::trunc);
    if (!out) {
        spdlog::error("PggViewer: cannot write {}", g_filePath);
        return;
    }
    out << g_text;
    g_dirty = false;
    spdlog::info("PggViewer: saved {}", g_filePath);
}

pgg::GraphScope* currentScope() {
    if (g_dive.empty()) return &g_project.top;
    return g_project.scopeOf(g_dive.back());
}

std::string currentScopePath() { return g_dive.empty() ? std::string{} : g_dive.back(); }

std::string shortPathLabel(const std::string& path) {
    const size_t dot = path.rfind('.');
    return dot == std::string::npos ? path : path.substr(dot + 1);
}

// The E6 probe target of a node (PggTool --probe syntax): a def call probes
// its instance outputs, a binding inside a dive resolves as <ipath>.<local>.
std::string probePathFor(const pgg::GraphNode& n) {
    std::string base;
    switch (n.kind) {
        case pgg::GraphNode::Kind::DefCall:
            return n.instancePath;
        case pgg::GraphNode::Kind::Binding:
        case pgg::GraphNode::Kind::ZoneHeader:
        case pgg::GraphNode::Kind::Param:
            if (n.outputs.empty()) return {};
            base = n.outputs[0];
            break;
        case pgg::GraphNode::Kind::Output:
            base = n.name;
            break;
        default:
            return {};
    }
    const std::string scopePath = currentScopePath();
    return scopePath.empty() ? base : scopePath + "." + base;
}

// CLI value parsing for probe runs (same rules as PggTool --param).
pgg::Value parseCliValue(const std::string& v) {
    if (v == "true") return pgg::Value(true);
    if (v == "false") return pgg::Value(false);
    if (v.size() >= 5 && v.front() == '(' && v.back() == ')') {
        std::vector<float> comps;
        std::stringstream ss(v.substr(1, v.size() - 2));
        std::string item;
        bool ok = true;
        while (std::getline(ss, item, ',')) {
            char* end = nullptr;
            const float f = std::strtof(item.c_str(), &end);
            if (end == item.c_str() || *end != '\0') ok = false;
            comps.push_back(f);
        }
        if (ok && comps.size() == 2) return pgg::Value(glm::vec2(comps[0], comps[1]));
        if (ok && comps.size() == 3) return pgg::Value(glm::vec3(comps[0], comps[1], comps[2]));
        if (ok && comps.size() == 4) return pgg::Value(glm::vec4(comps[0], comps[1], comps[2], comps[3]));
        return pgg::Value(v);
    }
    char* end = nullptr;
    const long long iv = std::strtoll(v.c_str(), &end, 10);
    if (end && *end == '\0' && end != v.c_str()) return pgg::Value(static_cast<int64_t>(iv));
    const float fv = std::strtof(v.c_str(), &end);
    if (end && *end == '\0' && end != v.c_str()) return pgg::Value(fv);
    return pgg::Value(v);
}

void runProbe(const std::string& inspector) {
    pgg::GraphScope* scope = currentScope();
    if (!scope || g_canvas.selected < 0) return;
    const pgg::GraphNode& n = scope->nodes[g_canvas.selected];
    const std::string target = probePathFor(n);
    if (target.empty()) {
        g_probeText = "this node is not probeable";
        return;
    }
    pgg::RunParams rp;
    for (const auto& [name, text] : g_paramValues)
        if (!text.empty()) rp.values.push_back({name, parseCliValue(text)});
    rp.importRoots = g_rpcImportRoots;
    rp.cache = g_memoryCache.get();
    rp.probes = {target + ":" + inspector};
    // Synchronous run by design (MVP): heavy graphs block the UI for seconds.
    pgg::RunResult r = pgg::runFile(g_filePath, rp);
    g_lastCacheHits = r.stats.cacheHits;
    g_lastCacheMisses = r.stats.cacheMisses;
    std::string out;
    for (const pgg::ProbeRecord& pr : r.probes) out += pr.origin + " " + pr.path + ": " + pr.text + "\n";
    for (const pgg::Diagnostic& d : r.diagnostics) out += pgg::formatDiagnostic(d, g_filePath) + "\n";
    if (out.empty()) out = "(no records)";
    g_probeText = std::move(out);
}

// Rebuilds the GPU geometry from the cached value (highlight / sdf resolution
// changes do not need a new run).
void rebuildPreviewGeometry(bool refit) {
    if (!g_previewHasValue) return;
    PreviewGeometry geo = buildPreviewGeometry(g_previewValue, g_previewOpts);
    g_previewGroups = geo.groups;
    g_previewGroupBBoxes = geo.groupBBoxes;
    g_previewHasColor = geo.hasColor;
    // Drop a highlight that the new value no longer carries.
    if (!g_previewOpts.highlightGroup.empty() &&
        std::find(geo.groups.begin(), geo.groups.end(), g_previewOpts.highlightGroup) == geo.groups.end())
        g_previewOpts.highlightGroup.clear();
    g_preview.setGeometry(geo, refit);
}

// --- camera targeting (A2) ------------------------------------------------------

CameraTargetSpec parseCameraTargetSpec(const std::string& text) {
    CameraTargetSpec spec;
    spec.raw = text;
    if (text.rfind("group:", 0) == 0) {
        spec.kind = CameraTargetSpec::Kind::Group;
        spec.name = text.substr(6);
    } else if (text.rfind("binding:", 0) == 0) {
        spec.kind = CameraTargetSpec::Kind::Binding;
        spec.name = text.substr(8);
    } else {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (std::sscanf(text.c_str(), "%f,%f,%f", &x, &y, &z) == 3) {
            spec.kind = CameraTargetSpec::Kind::Point;
            spec.point = glm::vec3(x, y, z);
        }
    }
    if (spec.kind != CameraTargetSpec::Kind::Point && spec.name.empty())
        spec.kind = CameraTargetSpec::Kind::None;
    return spec;
}

// Resolves a group target against the last build's per-group bboxes: exact
// "<domain>:<name>" first, then a bare-name match (an ambiguous bare name
// takes the first sorted key and logs the ambiguity).
bool resolveGroupTarget(const std::string& name, glm::vec3& outCenter, float& outRadius) {
    auto found = g_previewGroupBBoxes.end();
    if (const auto it = g_previewGroupBBoxes.find(name); it != g_previewGroupBBoxes.end()) {
        found = it;
    } else {
        for (auto jt = g_previewGroupBBoxes.begin(); jt != g_previewGroupBBoxes.end(); ++jt) {
            const size_t colon = jt->first.find(':');
            const std::string bare = colon == std::string::npos ? jt->first : jt->first.substr(colon + 1);
            if (bare != name) continue;
            if (found != g_previewGroupBBoxes.end())
                spdlog::warn("PggViewer: target group '{}' is ambiguous (taking '{}', also '{}')", name,
                             found->first, jt->first);
            else
                found = jt;
        }
    }
    if (found == g_previewGroupBBoxes.end()) return false;
    outCenter = (found->second.first + found->second.second) * 0.5f;
    outRadius = glm::length(found->second.second - found->second.first) * 0.5f;
    return true;
}

// Pulls the binding of a binding: camera target and takes its bbox (sdf is
// meshed at the default preview voxel). Resolved once per load — zones are
// uncached by design, so re-pulling on every rebuild would repeat the cost.
bool resolveBindingTarget(const std::string& path, glm::vec3& outCenter, float& outRadius) {
    if (!g_bindingTargetResolved) {
        pgg::RunParams rp;
        for (const auto& [name, text] : g_paramValues)
            if (!text.empty()) rp.values.push_back({name, parseCliValue(text)});
        rp.importRoots = g_rpcImportRoots;
        rp.cache = g_memoryCache.get();
        rp.pulls = {path};
        pgg::RunResult r = pgg::runFile(g_filePath, rp);
        bool found = false;
        pgg::Value value;
        for (const pgg::RunOutput& o : r.pulled) {
            const pgg::ScalarType base = pgg::valueBase(o.value);
            if (base == pgg::ScalarType::Geo || base == pgg::ScalarType::Sdf) {
                value = o.value;
                found = true;
                break;
            }
        }
        if (!found) {
            spdlog::warn("PggViewer: preview-target binding '{}' gave no geometry value", path);
            return false;
        }
        const PreviewGeometry pg = buildPreviewGeometry(value, PreviewBuildOptions{});
        if (!pg.ok) {
            spdlog::warn("PggViewer: preview-target binding '{}' has nothing to bound ({})", path, pg.summary);
            return false;
        }
        g_bindingTargetCenter = (pg.bmin + pg.bmax) * 0.5f;
        g_bindingTargetRadius = std::max(1e-3f, glm::length(pg.bmax - pg.bmin) * 0.5f);
        g_bindingTargetResolved = true;
    }
    outCenter = g_bindingTargetCenter;
    outRadius = g_bindingTargetRadius;
    return true;
}

// Re-applies the CLI/RPC camera target after a preview (re)build — the target
// must survive runPreview's refit. An unresolvable target falls back to
// fit=all with a warning.
void applyCameraTarget() {
    if (g_cameraTarget.kind == CameraTargetSpec::Kind::None) return;
    glm::vec3 center{0.0f};
    float radius = 1.0f;
    bool ok = false;
    switch (g_cameraTarget.kind) {
        case CameraTargetSpec::Kind::Point:
            center = g_cameraTarget.point;
            radius = g_preview.sceneRadius();
            ok = true;
            break;
        case CameraTargetSpec::Kind::Group:
            ok = resolveGroupTarget(g_cameraTarget.name, center, radius);
            break;
        case CameraTargetSpec::Kind::Binding:
            ok = resolveBindingTarget(g_cameraTarget.name, center, radius);
            break;
        default:
            break;
    }
    if (ok) {
        g_preview.setTarget(center, radius);
    } else {
        spdlog::warn("PggViewer: preview target '{}' not resolved — falling back to fit=all", g_cameraTarget.raw);
        g_preview.setFitMode(PreviewFitMode::All);
        g_preview.fit();
    }
}

// Pulls the value at `target` (probe-path syntax) with a synchronous run and
// shows it. Same MVP trade-off as the probes: heavy graphs block the UI.
void runPreview(const std::string& target) {
    if (target.empty() || g_filePath.empty()) return;
    pgg::RunParams rp;
    for (const auto& [name, text] : g_paramValues)
        if (!text.empty()) rp.values.push_back({name, parseCliValue(text)});
    rp.importRoots = g_rpcImportRoots;
    rp.cache = g_memoryCache.get();
    rp.pulls = {target};
    const uint64_t t0 = stm_now();
    pgg::RunResult r = pgg::runFile(g_filePath, rp);
    const double ms = stm_ms(stm_diff(stm_now(), t0));
    g_lastRunMs = ms;
    g_lastCacheHits = r.stats.cacheHits;
    g_lastCacheMisses = r.stats.cacheMisses;
    g_lastRunDiags = r.diagnostics;

    const bool newTarget = target != g_previewTarget;
    g_previewTarget = target;
    g_previewHasValue = false;
    for (const pgg::RunOutput& o : r.pulled) {
        const pgg::ScalarType base = pgg::valueBase(o.value);
        if (base == pgg::ScalarType::Geo || base == pgg::ScalarType::Sdf) {
            g_previewValue = o.value;
            g_previewHasValue = true;
            break;
        }
    }
    if (!g_previewHasValue) {
        g_preview.clear();
        std::string why;
        bool unboundParam = false;
        for (const pgg::Diagnostic& d : r.diagnostics) {
            if (d.isWarning) continue;
            why += (why.empty() ? "" : "\n") + d.code + " " + d.message;
            unboundParam |= d.code == "E604" && d.message.find("no default") != std::string::npos;
        }
        if (unboundParam) why += "\n-> set the value in the Params section of the side panel";
        if (why.empty()) why = r.pulled.empty() ? "no value" : "value has no geometry (" +
                                                              std::string(pgg::scalarName(pgg::valueBase(r.pulled[0].value))) + ")";
        g_lastPreviewError = why;
        g_preview.setSummary(target + ": run failed");
        g_preview.setError(why);
        g_showPreview = true;
        g_framesSincePreviewRun = 0;
        return;
    }
    g_lastPreviewError.clear();
    g_preview.setError({});
    rebuildPreviewGeometry(newTarget);
    applyCameraTarget();
    char buf[64];
    std::snprintf(buf, sizeof(buf), "  [%.0f ms]", ms);
    g_preview.setSummary(target + ": " + g_preview.summary() + buf);
    g_showPreview = true;
    g_framesSincePreviewRun = 0;
}

// Open... starts next to the current file; with nothing loaded — at the
// product examples (resources/pgg), else at the test corpus, else at cwd.
void openFileDialog() {
    std::filesystem::path start;
    if (!g_filePath.empty()) {
        start = std::filesystem::path(g_filePath).parent_path();
    } else {
        std::error_code ec;
        start = findPggResourcesDir(std::filesystem::current_path(ec));
        if (start.empty()) start = findPggCorpusDir(std::filesystem::current_path(ec));
    }
    fileDialogOpen(g_fileDialog, start);
}

void diveTo(const std::string& instancePath) {
    g_dive.push_back(instancePath);
    g_canvas.selected = -1;
    g_probeText.clear();
    g_needFitView = true;
}

void diveUpTo(size_t level) {
    if (level < g_dive.size()) {
        g_dive.resize(level);
        g_canvas.selected = -1;
        g_probeText.clear();
        g_needFitView = true;
    }
}

// --- ImGui --------------------------------------------------------------------------

void drawPanel(int w, int h) {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(kPanelWidth, static_cast<float>(h)), ImGuiCond_Always);
    ImGui::Begin("PggViewer", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize);

    const ImGuiIO& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S) && g_dirty && !g_filePath.empty()) saveFile();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O) && !g_fileDialog.open) openFileDialog();

    // File.
    if (ImGui::Button("Open...")) openFileDialog();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Browse for a .pgg file (Ctrl+O)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::InputText("##path", g_pathBuf, sizeof(g_pathBuf), ImGuiInputTextFlags_EnterReturnsTrue) &&
        g_pathBuf[0] != '\0')
        loadFile(g_pathBuf);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Path of a .pgg file; Enter or Load to open");
    if (ImGui::Button("Load") && g_pathBuf[0] != '\0') loadFile(g_pathBuf);
    ImGui::SameLine();
    if (ImGui::Button("Reload") && !g_filePath.empty()) loadFile(g_filePath);
    ImGui::SameLine();
    if (!g_dirty) ImGui::BeginDisabled();
    if (ImGui::Button("Save")) saveFile();
    if (!g_dirty) ImGui::EndDisabled();
    if (g_dirty) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.75f, 0.3f, 1.0f), "(modified)");
    }
    if (g_filePath.empty()) ImGui::TextDisabled("no file loaded");
    if (const auto picked = fileDialogDraw(g_fileDialog)) loadFile(picked->string());

    // Breadcrumb (dive path).
    ImGui::Separator();
    if (ImGui::SmallButton("top")) diveUpTo(0);
    for (size_t i = 0; i < g_dive.size(); ++i) {
        ImGui::SameLine();
        ImGui::TextDisabled("/");
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::SmallButton(shortPathLabel(g_dive[i]).c_str())) diveUpTo(i);
        ImGui::PopID();
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !io.WantTextInput && !g_dive.empty())
        diveUpTo(g_dive.size() - 1);

    // Probe panel.
    if (ImGui::CollapsingHeader("Probe", ImGuiTreeNodeFlags_DefaultOpen)) {
        pgg::GraphScope* scope = currentScope();
        const pgg::GraphNode* sel =
            (scope && g_canvas.selected >= 0) ? &scope->nodes[g_canvas.selected] : nullptr;
        if (!sel) {
            ImGui::TextDisabled("select a node on the canvas");
        } else {
            const std::string target = probePathFor(*sel);
            ImGui::Text("node: %s", !sel->name.empty() ? sel->name.c_str()
                                                       : (sel->outputs.empty() ? "?" : sel->outputs[0].c_str()));
            ImGui::TextDisabled("op: %s   line: %d", sel->op.c_str(), sel->span.line);
            if (target.empty()) {
                ImGui::TextDisabled("not probeable");
            } else {
                ImGui::TextWrapped("target: %s", target.c_str());
                for (const char* insp : {"schema", "stats", "coverage", "table"}) {
                    if (insp[0] != 's') ImGui::SameLine();
                    if (ImGui::SmallButton(insp)) runProbe(insp);
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Preview")) runPreview(target);
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Render this node's geometry in the Preview window");
            }
        }
        if (!g_probeText.empty()) {
            ImGui::BeginChild("##probeout", ImVec2(0.0f, 140.0f), true);
            ImGui::TextWrapped("%s", g_probeText.c_str());
            ImGui::EndChild();
        }
    }

    // Geometry preview options.
    if (ImGui::CollapsingHeader("Preview", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("show pane", &g_showPreview);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Docked preview pane below the graph (drag the splitter to resize)");
        ImGui::SameLine();
        ImGui::Checkbox("auto on select", &g_autoPreview);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Pull and render the selected node's value as soon as it is selected\n"
                              "(synchronous run: heavy graphs pause the UI)");
        if (!g_previewTarget.empty()) ImGui::TextDisabled("showing: %s", g_previewTarget.c_str());
        // Group highlight.
        const std::string& cur = g_previewOpts.highlightGroup;
        if (ImGui::BeginCombo("highlight", cur.empty() ? "(none)" : cur.c_str())) {
            if (ImGui::Selectable("(none)", cur.empty())) {
                g_previewOpts.highlightGroup.clear();
                rebuildPreviewGeometry(false);
            }
            for (const std::string& gname : g_previewGroups) {
                if (ImGui::Selectable(gname.c_str(), gname == cur)) {
                    g_previewOpts.highlightGroup = gname;
                    rebuildPreviewGeometry(false);
                }
            }
            ImGui::EndCombo();
        }
        if (g_previewGroups.empty()) ImGui::TextDisabled("(no groups on the previewed geometry)");
        // Normal source: corner N (flat compute_normals) > point @N > face normals.
        {
            static const char* kShading[] = {"auto", "smooth", "flat"};
            int sh = static_cast<int>(g_previewOpts.shading);
            if (ImGui::Combo("shading", &sh, kShading, 3)) {
                g_previewOpts.shading = static_cast<PreviewShading>(sh);
                rebuildPreviewGeometry(false);
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("auto: corner N (compute_normals flat) > point @N > face normals\n"
                                  "smooth: point @N > face normals\n"
                                  "flat: face normals only (faceted look for welded boxes)");
        }
        // Surface color from the @Cd attribute.
        if (ImGui::Checkbox("colors (@Cd)", &g_previewOpts.vertexColors)) rebuildPreviewGeometry(false);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Albedo from the vec3 attribute @Cd (points/corners/faces/detail; face colors unweld the mesh).\n"
                              "Off or absent: neutral grey.");
        if (g_previewOpts.vertexColors && g_previewHasValue && !g_previewHasColor) {
            ImGui::SameLine();
            ImGui::TextDisabled("(no @Cd on the previewed geometry)");
        }
        // sdf meshing resolution (only matters for sdf values).
        if (ImGui::SliderInt("sdf voxels", &g_previewOpts.sdfResolution, 16, 256)) {
            if (g_previewHasValue && pgg::valueBase(g_previewValue) == pgg::ScalarType::Sdf)
                rebuildPreviewGeometry(false);
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Longest bbox axis in voxels when meshing an sdf value for preview");
    }

    // Launch params of the file (used by probe runs).
    // A param without a default and without a value blocks every run (E604):
    // keep the section open and flag the field until it is filled in.
    bool missingParam = false;
    for (const auto& [name, text] : g_paramValues) missingParam |= text.empty();
    if (missingParam) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
    if (!g_paramValues.empty() && ImGui::CollapsingHeader("Params")) {
        if (missingParam)
            ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                               "required: params without a default must be set before a run");
        for (auto& [name, text] : g_paramValues) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", text.c_str());
            const bool missing = text.empty();
            if (missing) ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.45f, 0.15f, 0.12f, 1.0f));
            if (ImGui::InputTextWithHint(name.c_str(), missing ? "required" : "", buf, sizeof(buf))) text = buf;
            if (missing) ImGui::PopStyleColor();
            // Re-run the shown preview once the edit is committed (focus leaves
            // the field / Enter), not on every keystroke.
            if (ImGui::IsItemDeactivatedAfterEdit() && g_autoPreview && !g_previewTarget.empty())
                runPreview(g_previewTarget);
        }
    }

    // Diagnostics.
    if (ImGui::CollapsingHeader("Diagnostics")) {
        int errors = 0, warnings = 0;
        for (const pgg::Diagnostic& d : g_allDiags) (d.isWarning ? warnings : errors) += 1;
        ImGui::Text("%d error(s), %d warning(s)", errors, warnings);
        ImGui::BeginChild("##diags", ImVec2(0.0f, 160.0f), true);
        for (const pgg::Diagnostic& d : g_allDiags) {
            const ImVec4 c = d.isWarning ? ImVec4(0.9f, 0.75f, 0.3f, 1.0f) : ImVec4(0.95f, 0.4f, 0.35f, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Text, c);
            ImGui::TextWrapped("%s", pgg::formatDiagnostic(d, g_filePath).c_str());
            ImGui::PopStyleColor();
        }
        if (g_allDiags.empty()) ImGui::TextDisabled("(clean)");
        ImGui::EndChild();
    }

    if (ImGui::CollapsingHeader("Help")) {
        ImGui::TextWrapped("Open... / Ctrl+O: browse for a .pgg file. Ctrl+S: save layout hints.");
        ImGui::TextWrapped("The right region is split: graph on top, preview pane below; drag the splitter to "
                           "resize (default 1:2). Preview pane: LMB drag orbit, RMB/MMB drag pan, wheel zoom, Fit resets. "
                           "Meshes/points render directly, instances are realized, sdf is meshed at 'sdf voxels'.");
        ImGui::TextWrapped("LMB drag node: move (writes a # @pos hint on release; Save persists).");
        ImGui::TextWrapped("LMB drag empty / RMB drag: pan. Wheel: zoom to cursor.");
        ImGui::TextWrapped("Double-click a def node: dive into the instance body. Esc / breadcrumb: back.");
        ImGui::TextWrapped("Click a node, then run an inspector in the Probe section (E6 probes).");
        ImGui::TextWrapped("Orange wire: zone state loop. Blue dot: the node has a layout hint.");
    }
    ImGui::End();
}

void drawCanvasWindow(int w, float graphH) {
    const float x0 = panelWidth();
    ImGui::SetNextWindowPos(ImVec2(x0, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(w) - x0, graphH), ImGuiCond_Always);
    ImGui::Begin("##graph", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);

    pgg::GraphScope* scope = currentScope();
    const ImVec2 view = ImGui::GetWindowSize();
    if (scope && g_needFitView) {
        if (g_cliZoom || g_cliCenter) {
            // Deterministic framing for screenshot comparisons. --zoom alone
            // keeps the content-center framing at that zoom level.
            g_canvas.zoom = g_cliZoom.value_or(1.0f);
            ImVec2 c = g_cliCenter.value_or(ImVec2(0.0f, 0.0f));
            if (!g_cliCenter) {
                float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
                for (const pgg::GraphNode& n : scope->nodes) {
                    minX = std::min(minX, n.x);
                    minY = std::min(minY, n.y);
                    maxX = std::max(maxX, n.x);
                    maxY = std::max(maxY, n.y);
                }
                c = ImVec2((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
            }
            g_canvas.offsetX = view.x * 0.5f - c.x * g_canvas.zoom;
            g_canvas.offsetY = view.y * 0.5f - c.y * g_canvas.zoom;
        } else {
            canvasFitView(*scope, g_layout, g_canvas, view.x, view.y);
        }
        g_needFitView = false;
    }
    if (scope && !g_filePath.empty()) {
        const bool editable = scope->originFile.empty();
        const GraphCanvasResult res = drawGraphCanvas(*scope, g_layout, g_canvas, editable);
        if (res.diveNode >= 0) {
            const pgg::GraphNode& n = scope->nodes[res.diveNode];
            if (!n.instancePath.empty()) diveTo(n.instancePath);
        }
        if (res.hintNode >= 0) {
            pgg::GraphNode& n = scope->nodes[res.hintNode];
            g_text = pgg::applyPosHint(g_text, n.span.line, static_cast<int>(std::lround(n.x)),
                                       static_cast<int>(std::lround(n.y)));
            n.hasHint = true;
            n.hintX = n.x;
            n.hintY = n.y;
            g_dirty = true;
        }
    } else {
        // Empty state: tell the user how to get a graph on screen.
        const char* line1 = "No .pgg file loaded";
        const char* line2 = "Open... (Ctrl+O), type a path in the panel, or pass a file on the command line";
        const ImVec2 s1 = ImGui::CalcTextSize(line1);
        const ImVec2 s2 = ImGui::CalcTextSize(line2);
        ImGui::SetCursorPos(ImVec2((view.x - s1.x) * 0.5f, view.y * 0.5f - s1.y));
        ImGui::TextDisabled("%s", line1);
        ImGui::SetCursorPos(ImVec2((view.x - s2.x) * 0.5f, view.y * 0.5f + s1.y * 0.5f));
        ImGui::TextDisabled("%s", line2);
    }
    ImGui::End();
}

// The right region is a split view: graph canvas on top, preview pane below,
// separated by a draggable splitter (default ratio 1:2). The splitter is a
// thin window drawn last so its hover/drag wins over both panes.
void drawSplitter(int w, int h, float graphH) {
    const float x0 = panelWidth();
    ImGui::SetNextWindowPos(ImVec2(x0, graphH), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(w) - x0, kSplitterHeight), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##splitter", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground |
                     ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::InvisibleButton("##split", ImGui::GetWindowSize(), ImGuiButtonFlags_MouseButtonLeft);
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemActive();
    if (hot) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.0f)) {
        const float usable = static_cast<float>(h) - kSplitterHeight;
        g_splitRatio = std::clamp(ImGui::GetIO().MousePos.y / usable, 0.12f, 0.88f);
    }
    const ImVec2 mn = ImGui::GetWindowPos();
    const float y = mn.y + kSplitterHeight * 0.5f - 1.0f;
    const ImU32 col = hot ? IM_COL32(150, 160, 180, 255) : IM_COL32(80, 85, 95, 255);
    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(mn.x, y), ImVec2(mn.x + ImGui::GetWindowWidth(), y + 2.0f), col);
    ImGui::End();
    ImGui::PopStyleVar();
}

// Docked preview pane under the splitter.
void drawPreviewPane(int w, int h, float graphH) {
    if (!g_showPreview) return;
    const float x0 = panelWidth();
    ImGui::SetNextWindowPos(ImVec2(x0, graphH + kSplitterHeight), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(w) - x0,
                                    static_cast<float>(h) - graphH - kSplitterHeight),
                           ImGuiCond_Always);
    ImGui::Begin("##preview", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);
    g_preview.drawWindowContents();
    ImGui::End();
}

// Auto-preview: the selection changed -> pull the node's value.
void updateAutoPreview() {
    if (!g_autoPreview || g_filePath.empty()) return;
    pgg::GraphScope* scope = currentScope();
    const std::string scopePath = currentScopePath();
    if (g_canvas.selected == g_previewLastSelected && scopePath == g_previewLastScope) return;
    g_previewLastSelected = g_canvas.selected;
    g_previewLastScope = scopePath;
    if (!scope || g_canvas.selected < 0) return;
    const std::string target = probePathFor(scope->nodes[g_canvas.selected]);
    if (!target.empty() && target != g_previewTarget) runPreview(target);
}

// Portable --shot capture. GL reads back the default framebuffer (the
// documented sokol/GL trap keeps this in the TU that owns SOKOL_IMPL — glad
// must never join them); Metal/D3D11 read back the drawable/backbuffer of the
// frame's swapchain (stashed in g_frameSwapchain — sokol has no readback API
// and this sokol version does not implement the sapp_metal/d3d11 getters).
// The current frame's swapchain descriptor, stashed by frame() for capturePng
// (Metal drawable / D3D11 render view; only valid during the frame callback).
sg_swapchain g_frameSwapchain = {};

void swizzleBgraToRgba(std::vector<std::uint8_t>& pixels) {
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
}

bool writePng(const char* path, int width, int height, const std::vector<std::uint8_t>& pixels) {
    const int ok = stbi_write_png(path, width, height, 4, pixels.data(), width * 4);
    if (!ok) {
        spdlog::error("capturePng: stbi_write_png failed for {}", path);
        return false;
    }
    return true;
}

bool capturePng(const char* path) {
    const int width = sapp_width();
    const int height = sapp_height();
    if (!path || path[0] == '\0' || width <= 0 || height <= 0) return false;
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    const bool ok = writePng(path, width, height, pixels);
    stbi_flip_vertically_on_write(0);
    return ok;
#elif defined(SOKOL_METAL) && defined(__APPLE__)
    // Valid only inside frame() (the drawable lives in sokol_app's per-frame
    // autorelease pool); capturePng is called from frame() right after the
    // stash, same pool.
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)g_frameSwapchain.metal.current_drawable;
    if (drawable == nil) {
        spdlog::error("capturePng: no Metal drawable in the current frame");
        return false;
    }
    // The just-committed frame may still be shading on sokol's command queue,
    // and cross-queue ordering is not guaranteed — wait for the drawable to be
    // presented (all writes complete) before blitting. If the race went the
    // other way (presented before the handler was added), proceed after the
    // timeout; the content is final by then.
    if (drawable.presentedTime <= 0.0) {
        dispatch_semaphore_t presented = dispatch_semaphore_create(0);
        [drawable addPresentedHandler:^(id<MTLDrawable>) { dispatch_semaphore_signal(presented); }];
        dispatch_semaphore_wait(presented, dispatch_time(DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC));
    }
    id<MTLTexture> src = drawable.texture;
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    MTLTextureDescriptor* desc = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                    width:static_cast<NSUInteger>(width)
                                                                                   height:static_cast<NSUInteger>(height)
                                                                                mipmapped:NO];
    desc.storageMode = MTLStorageModeShared;
    desc.usage = MTLTextureUsageShaderRead;
    id<MTLTexture> dst = [device newTextureWithDescriptor:desc];
    id<MTLCommandQueue> queue = [device newCommandQueue];
    id<MTLCommandBuffer> cmd = [queue commandBuffer];
    id<MTLBlitCommandEncoder> blit = [cmd blitCommandEncoder];
    [blit copyFromTexture:src
              sourceSlice:0
              sourceLevel:0
             sourceOrigin:MTLOriginMake(0, 0, 0)
               sourceSize:MTLSizeMake(static_cast<NSUInteger>(width), static_cast<NSUInteger>(height), 1)
                toTexture:dst
         destinationSlice:0
         destinationLevel:0
        destinationOrigin:MTLOriginMake(0, 0, 0)];
    [blit endEncoding];
    [cmd commit];
    [cmd waitUntilCompleted];
    if (cmd.status == MTLCommandBufferStatusError) {
        spdlog::error("capturePng: Metal blit failed");
        return false;
    }
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    [dst getBytes:pixels.data()
      bytesPerRow:static_cast<NSUInteger>(width * 4)
       fromRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(width), static_cast<NSUInteger>(height))
      mipmapLevel:0];
    swizzleBgraToRgba(pixels);  // CAMetalLayer is BGRA8; row 0 is the top — no flip
    return writePng(path, width, height, pixels);
#elif defined(SOKOL_D3D11)
    // NOTE: written without a Windows machine at hand — verify on first use.
    // Ordering is free: CopyResource on the same immediate context is
    // serialized after the frame's commands, and Map blocks until done.
    ID3D11RenderTargetView* rtv = static_cast<ID3D11RenderTargetView*>(g_frameSwapchain.d3d11.render_view);
    if (rtv == nullptr) {
        spdlog::error("capturePng: no D3D11 render view in the current frame");
        return false;
    }
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT hr = res != nullptr ? res->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer))
                                : E_POINTER;
    if (res != nullptr) res->Release();
    if (FAILED(hr) || backbuffer == nullptr) {
        spdlog::error("capturePng: D3D11 backbuffer QueryInterface failed");
        return false;
    }
    ID3D11Device* device = nullptr;
    backbuffer->GetDevice(&device);
    ID3D11DeviceContext* context = nullptr;
    device->GetImmediateContext(&context);
    D3D11_TEXTURE2D_DESC desc = {};
    backbuffer->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    hr = device->CreateTexture2D(&desc, nullptr, &staging);
    if (FAILED(hr) || staging == nullptr) {
        spdlog::error("capturePng: D3D11 staging texture creation failed");
        context->Release();
        device->Release();
        backbuffer->Release();
        return false;
    }
    context->CopyResource(staging, backbuffer);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    bool ok = false;
    if (SUCCEEDED(hr)) {
        std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
        const auto* srcRow = static_cast<const std::uint8_t*>(mapped.pData);
        for (int y = 0; y < height; ++y) {
            std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width * 4, srcRow + static_cast<std::size_t>(y) * mapped.RowPitch, static_cast<std::size_t>(width) * 4);
        }
        context->Unmap(staging, 0);
        swizzleBgraToRgba(pixels);  // backbuffer is B8G8R8A8; row 0 is the top — no flip
        ok = writePng(path, width, height, pixels);
    } else {
        spdlog::error("capturePng: D3D11 Map failed");
    }
    staging->Release();
    context->Release();
    device->Release();
    backbuffer->Release();
    return ok;
#else
    spdlog::error("capturePng: --shot is not implemented for this backend");
    return false;
#endif
}

// --- RPC helpers (--serve) ----------------------------------------------------------

// Repository root for default RPC output dirs (tmp/pgg_rpc_shots,
// tmp/pgg_rpc_source): .git walk-up from the cwd, like SmokeTest's
// findRepoRoot; falls back to the cwd.
std::filesystem::path repoRoot() {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::current_path(ec);
    if (ec) return ".";
    for (int i = 0; i < 12; ++i) {
        if (std::filesystem::exists(dir / ".git", ec)) return dir;
        if (!dir.has_parent_path() || dir == dir.parent_path()) break;
        dir = dir.parent_path();
    }
    return ".";
}

// Wall clock for the RPC layer (std::chrono, not sokol_time: the smoke test
// runs handlers without init()/stm_setup()).
double wallNowSec() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool diagsHaveErrors(const std::vector<pgg::Diagnostic>& diags) {
    for (const pgg::Diagnostic& d : diags)
        if (!d.isWarning) return true;
    return false;
}

nlohmann::json diagnosticsJson(const std::vector<pgg::Diagnostic>& diags) {
    nlohmann::json arr = nlohmann::json::array();
    for (const pgg::Diagnostic& d : diags) {
        nlohmann::json j = {{"code", d.code},      {"line", d.span.line}, {"col", d.span.col},
                            {"warning", d.isWarning}, {"message", d.message}};
        if (!d.hint.empty()) j["hint"] = d.hint;
        arr.push_back(std::move(j));
    }
    return arr;
}

// The static prefix of pgg::run (engine.cpp): parse findings, then import
// closure, expansion and typecheck — no Engine::run, so a broken file is
// answered in milliseconds without evaluating the graph.
nlohmann::json staticCheckJson(const pgg::Document& doc, const std::vector<std::string>& importRoots,
                               const std::vector<std::string>& boundParams) {
    const double t0 = wallNowSec();
    std::vector<pgg::Diagnostic> diags = doc.diagnostics;
    if (doc.file && !doc.hasErrors()) {
        pgg::ModuleClosure closure;
        const pgg::ModuleClosure* closurePtr = nullptr;
        if (pgg::hasImports(*doc.file)) {
            closure = pgg::loadModuleClosure(*doc.file, importRoots, diags);
            closurePtr = &closure;
        }
        pgg::FlatProgram flat = pgg::expandProgram(*doc.file, closurePtr, diags);
        if (!diagsHaveErrors(diags)) {
            std::vector<size_t> runtimeContracts;
            pgg::typecheckFlat(flat, boundParams, diags, runtimeContracts);
        }
    }
    return {{"diagnostics", diagnosticsJson(diags)},
            {"has_errors", diagsHaveErrors(diags)},
            {"ms", (wallNowSec() - t0) * 1000.0}};
}

nlohmann::json vec3Json(const glm::vec3& v) { return nlohmann::json::array({v.x, v.y, v.z}); }

// Value-level stats of a pulled value (render/export responses): kind,
// counts, bbox, groups ("<domain>:<name>", sorted for determinism).
nlohmann::json valueStatsJson(const pgg::Value& v, double ms) {
    nlohmann::json s;
    const pgg::ScalarType base = pgg::valueBase(v);
    s["kind"] = pgg::scalarName(base);
    s["pts"] = 0;
    s["tri"] = 0;
    s["groups"] = nlohmann::json::array();
    if (base == pgg::ScalarType::Geo) {
        const pgg::Geo& g = *pgg::asGeo(v);
        s["kind"] = pgg::geoKindName(g.kind);
        s["pts"] = g.pointCount();
        size_t tri = 0;
        if (g.kind == pgg::GeoKind::Mesh && g.faceOffsets)
            for (size_t f = 0; f < g.faceCount(); ++f) {
                const int32_t corners = (*g.faceOffsets)[f + 1] - (*g.faceOffsets)[f];
                if (corners >= 3) tri += static_cast<size_t>(corners - 2);
            }
        s["tri"] = tri;
        if (g.pointCount() > 0) {
            glm::vec3 mn, mx;
            pgg::geoBBox(g, mn, mx);
            s["bbox"] = {{"min", vec3Json(mn)}, {"max", vec3Json(mx)}};
        }
        std::vector<std::string> groups;
        for (pgg::Domain d : {pgg::Domain::Points, pgg::Domain::Corners, pgg::Domain::Faces}) {
            const pgg::GroupSet* gs = g.groups(d);
            if (!gs) continue;
            for (const auto& [name, column] : gs->columns)
                groups.push_back(std::string(pgg::domainName(d)) + ":" + name);
        }
        std::sort(groups.begin(), groups.end());
        groups.erase(std::unique(groups.begin(), groups.end()), groups.end());
        s["groups"] = groups;
    } else if (base == pgg::ScalarType::Sdf) {
        glm::vec3 mn, mx;
        pgg::asSdf(v)->conservativeBBox(mn, mx);
        if (mn.x <= mx.x && mn.y <= mx.y && mn.z <= mx.z)
            s["bbox"] = {{"min", vec3Json(mn)}, {"max", vec3Json(mx)}};
    }
    s["ms"] = ms;
    return s;
}

// JSON value -> param field text (same syntax as the panel fields / --param).
std::string jsonToParamText(const nlohmann::json& v) {
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer()) return std::to_string(v.get<int64_t>());
    if (v.is_number_unsigned()) return std::to_string(v.get<uint64_t>());
    if (v.is_number_float()) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%g", v.get<double>());
        return buf;
    }
    if (v.is_array()) {
        std::string out = "(";
        for (size_t i = 0; i < v.size(); ++i) out += (i ? ", " : "") + jsonToParamText(v[i]);
        return out + ")";
    }
    return v.dump();
}

nlohmann::json paramsJson() {
    nlohmann::json out = nlohmann::json::object();
    for (const auto& [name, text] : g_paramValues) out[name] = text;
    return out;
}

std::vector<std::string> boundParamNames() {
    std::vector<std::string> names;
    for (const auto& [name, text] : g_paramValues)
        if (!text.empty()) names.push_back(name);
    return names;
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("PggViewer: init()");

    stm_setup();
    g_state.lastTime = stm_now();
    g_startTimeSec = wallNowSec();

    // Session-wide run cache (A1): outlives loadFile/Reload of the same file.
    // Capacity must cover the file's cacheable binding count, otherwise the
    // LRU thrashes (cottage_mansard evaluates ~2.2k bindings: at 512 the warm
    // re-run is not faster than the cold one); entries share immutable
    // columns, so a larger capacity is mostly cheap.
    g_memoryCache = std::make_unique<pgg::MemoryCache>(4096);

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = slog_func;
    sg_setup(&desc);
    g_state.gfxOk = sg_isvalid();
    if (!g_state.gfxOk) {
        spdlog::error("PggViewer: sg_setup FAILED");
        return;
    }

    if (!g_noUi) {
        simgui_desc_t imgui_desc = {};
        // The canvas draws every node box and bezier wire of the graph each
        // frame; a ~370-node example file (resources/pgg/cottage.pgg) already
        // exceeds the simgui default of 65536 vertices — on overflow simgui
        // silently drops the remaining ImGui command lists (side panel and
        // graph vanished, only the preview pane survived). 1M vertices = 20 MB
        // vertex + 6 MB index staging, plenty for any example graph.
        imgui_desc.max_vertices = 1 << 20;
        imgui_desc.logger.func = slog_func;  // surfaces BUFFER_OVERFLOW instead of hiding it
        simgui_setup(&imgui_desc);
        g_state.imguiOk = true;
        g_preview.init();
    }

    if (!g_pendingLoad.empty()) loadFile(g_pendingLoad);
    // --dive=<instance path>: open a def body directly (deterministic shots).
    if (!g_cliDive.empty() && g_project.scopeOf(g_cliDive)) {
        g_dive.push_back(g_cliDive);
        g_needFitView = true;
    }
    // --preview=<path>: pull and show a value at startup (shots / smoke by eye).
    if (g_cliOrbit) g_preview.setOrbit(g_cliOrbit->x, g_cliOrbit->y, g_cliOrbit->z);
    // A2 camera flags: ortho snaps after --preview-orbit (ortho wins the
    // angles), the target spec is applied by runPreview after the rebuild.
    if (!g_cliPreviewTarget.empty()) {
        g_cameraTarget = parseCameraTargetSpec(g_cliPreviewTarget);
        if (g_cameraTarget.kind == CameraTargetSpec::Kind::None)
            spdlog::warn("PggViewer: cannot parse --preview-target='{}' (want x,y,z | group:<name> | binding:<path>)",
                         g_cliPreviewTarget);
    }
    if (g_cliPreviewFit == "target") {
        g_preview.setFitMode(PreviewFitMode::Target);
    } else if (g_cliPreviewFit == "all") {
        g_preview.setFitMode(PreviewFitMode::All);
    } else if (!g_cliPreviewFit.empty()) {
        spdlog::warn("PggViewer: unknown --preview-fit='{}' (want all|target)", g_cliPreviewFit);
    } else if (g_cameraTarget.kind != CameraTargetSpec::Kind::None) {
        g_preview.setFitMode(PreviewFitMode::Target);  // default: fit the given target
    }
    if (!g_cliPreviewOrtho.empty()) {
        if (g_cliPreviewOrtho == "front") {
            g_preview.setProjection(PreviewProjection::OrthoFront);
        } else if (g_cliPreviewOrtho == "side") {
            g_preview.setProjection(PreviewProjection::OrthoSide);
        } else if (g_cliPreviewOrtho == "top") {
            g_preview.setProjection(PreviewProjection::OrthoTop);
        } else {
            spdlog::warn("PggViewer: unknown --preview-ortho='{}' (want front|side|top)", g_cliPreviewOrtho);
        }
    }
    if (g_cliPreviewWire) g_preview.setWireframe(true);
    if (!g_cliPreview.empty() && g_state.imguiOk) runPreview(g_cliPreview);

    // --serve[=host:port]: TCP RPC server, polled from frame() (A1). A busy
    // port is not fatal — log and run without the server, like the map editor.
    if (!g_serveAddress.empty()) {
        std::string host = "127.0.0.1";
        uint16_t port = ViewerRpcServer::kDefaultPort;
        const size_t colon = g_serveAddress.rfind(':');
        if (colon != std::string::npos) {
            host = g_serveAddress.substr(0, colon);
            if (host.empty()) host = "127.0.0.1";
            port = static_cast<uint16_t>(std::atoi(g_serveAddress.substr(colon + 1).c_str()));
        } else if (g_serveAddress.find_first_not_of("0123456789") == std::string::npos) {
            port = static_cast<uint16_t>(std::atoi(g_serveAddress.c_str()));
        } else {
            host = g_serveAddress;
        }
        g_rpc = std::make_unique<ViewerRpcServer>();
        registerPggViewerRpcHandlers(*g_rpc);
        if (!g_rpc->start(host, port)) {
            spdlog::error("PggViewer: --serve could not listen on {}:{} — running without the RPC server",
                          host, port);
            g_rpc.reset();
        }
#if defined(SOKOL_METAL) && defined(__APPLE__)
        if (g_rpc) {
            // macOS stops frame callbacks when the display idle-sleeps (and
            // App Naps the process), which would stall the RPC loop and any
            // pending render — hold a UserInitiated activity for the session.
            // A user-locked screen still stops frames (documented limit).
            [[NSProcessInfo processInfo] beginActivityWithOptions:NSActivityUserInitiated
                                                           reason:@"PggViewer --serve RPC server"];
        }
#endif
    }
}

void frame() {
    const uint64_t now = stm_now();
    g_state.dt = static_cast<float>(stm_sec(stm_diff(now, g_state.lastTime)));
    g_state.lastTime = now;

    // RPC commands run on the GUI thread; a render command may run a
    // synchronous pull here (MVP: heavy graphs freeze the window, same as the
    // interactive Preview button).
    if (g_rpc) g_rpc->poll();

    if (!g_state.gfxOk) return;

    const float dpi = std::max(sapp_dpi_scale(), 0.01f);
    const int w = static_cast<int>(std::lround(sapp_widthf() / dpi));
    const int h = static_cast<int>(std::lround(sapp_heightf() / dpi));

    if (g_state.imguiOk) {
        simgui_frame_desc_t fd = {};
        fd.width = sapp_width();
        fd.height = sapp_height();
        fd.delta_time = g_state.dt;
        fd.dpi_scale = dpi;
        simgui_new_frame(&fd);
        // --preview-size=W,H: the pane spans the right region, so only the
        // requested height maps to the split ratio (applied once at startup).
        if (g_cliPreviewSize.y > 0.0f) {
            g_splitRatio =
                std::clamp(1.0f - g_cliPreviewSize.y / static_cast<float>(h), 0.12f, 0.88f);
            g_cliPreviewSize = ImVec2(0.0f, 0.0f);
        }
        const float graphH =
            g_showPreview ? std::clamp(g_splitRatio, 0.12f, 0.88f) * (static_cast<float>(h) - kSplitterHeight)
                          : static_cast<float>(h);
        drawPanel(w, h);
        drawCanvasWindow(w, graphH);
        updateAutoPreview();
        drawPreviewPane(w, h, graphH);
        if (g_showPreview) drawSplitter(w, h, graphH);
        // Offscreen preview pass: outside (before) the swapchain pass that
        // draws the ImGui image referencing its target.
        g_preview.render();
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.1f, 0.11f, 0.13f, 1.0f};
    g_frameSwapchain = sglue_swapchain();  // stash for capturePng (single nextDrawable per frame)
    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = g_frameSwapchain;
    sg_begin_pass(&pass);
    if (g_state.imguiOk) simgui_render();
    sg_end_pass();
    sg_commit();
    ++g_framesSincePreviewRun;

    // RPC render phase 2: the frame with the new geometry is committed — grab
    // the framebuffer and answer the deferred client.
    if (g_pendingRender.active && g_framesSincePreviewRun >= 1) {
        const PendingRender pr = std::move(g_pendingRender);
        g_pendingRender = PendingRender{};
        if (capturePng(pr.outPath.c_str())) {
            nlohmann::json data = {{"path", pr.outPath},
                                   {"width", sapp_width()},
                                   {"height", sapp_height()},
                                   {"node", pr.node},
                                   {"stats", pr.stats},
                                   {"camera",
                                    {{"center", vec3Json(g_preview.center())},
                                     {"radius", g_preview.fitRadius()},
                                     {"distance", g_preview.distance()}}},
                                   {"cache", {{"hits", pr.cacheHits}, {"misses", pr.cacheMisses}}}};
            if (g_rpc) g_rpc->reply(pr.clientId, data);
            spdlog::info("PggViewer: RPC render {} -> {}", pr.node, pr.outPath);
        } else {
            if (g_rpc)
                g_rpc->replyError(pr.clientId, "capture_failed", "capturePng failed for " + pr.outPath);
            spdlog::error("PggViewer: RPC render capture failed ({})", pr.outPath);
        }
    }

    // Headless capture: the graph is laid out at load time, so a short
    // wall-time settle is enough before grabbing the framebuffer. A1: with
    // --preview and no explicit --shot-delay the first committed frame after
    // the (synchronous) preview run is enough; an explicit --shot-delay keeps
    // the old wall-time behaviour.
    if (!g_shotPath.empty()) {
        const bool ready = g_shotDelayExplicit || g_cliPreview.empty()
                               ? stm_sec(stm_now()) >= g_shotDelaySec
                               : g_framesSincePreviewRun >= 1;
        if (ready) {
            if (capturePng(g_shotPath.c_str())) {
                spdlog::info("PggViewer: screenshot saved to {}", g_shotPath);
            } else {
                spdlog::error("PggViewer: screenshot capture failed ({})", g_shotPath);
            }
            g_shotPath.clear();
            sapp_quit();
        }
    }
}

void cleanup() {
    if (g_rpc) {
        g_rpc->stop();
        g_rpc.reset();
    }
    if (g_state.imguiOk) {
        g_preview.shutdown();
        simgui_shutdown();
        g_state.imguiOk = false;
    }
    if (sg_isvalid()) sg_shutdown();
}

void event(const sapp_event* ev) {
    if (g_state.imguiOk) simgui_handle_event(ev);
}

std::optional<ImVec2> parseVec2Arg(const std::string& text) {
    const std::size_t comma = text.find(',');
    if (comma == std::string::npos) return std::nullopt;
    return ImVec2(static_cast<float>(std::atof(text.substr(0, comma).c_str())),
                  static_cast<float>(std::atof(text.substr(comma + 1).c_str())));
}

}  // namespace

// Registers the --serve command handlers (declared in SmokeTest.h so the
// smoke test can drive the same handlers on its own server instance). The
// handlers close over main.cpp's globals; the server class itself is
// stateless about the viewer.
void registerPggViewerRpcHandlers(ViewerRpcServer& server) {
    using nlohmann::json;

    server.on("ping", [](uint64_t, const json&) -> std::optional<json> {
        return json{{"pong", true}, {"app", "PggViewer"}, {"protocol", 1}};
    });

    server.on("status", [](uint64_t, const json&) -> std::optional<json> {
        return json{{"file", g_filePath},
                    {"params", paramsJson()},
                    {"cache",
                     {{"size", g_memoryCache ? g_memoryCache->size() : 0},
                      {"capacity", g_memoryCache ? g_memoryCache->capacity() : 0},
                      {"hits", g_lastCacheHits},
                      {"misses", g_lastCacheMisses}}},
                    {"preview", {{"target", g_previewTarget}, {"has_value", g_previewHasValue}}},
                    {"uptime_s", g_startTimeSec > 0.0 ? wallNowSec() - g_startTimeSec : 0.0}};
    });

    server.on("load", [](uint64_t, const json& args) -> std::optional<json> {
        std::string path;
        std::vector<std::string> roots;
        for (const json& r : args.value("lib_roots", json::array()))
            if (r.is_string()) roots.push_back(r.get<std::string>());
        if (args.contains("source")) {
            // load{source} goes through a temp file so every downstream
            // consumer (runPreview/runProbe read the file from disk) works
            // unchanged. The file's directory is the implicit import root.
            const std::filesystem::path dir = repoRoot() / "tmp" / "pgg_rpc_source";
            std::error_code ec;
            std::filesystem::create_directories(dir, ec);
            path = (dir / ("src_" + std::to_string(++g_srcCounter) + ".pgg")).string();
            std::ofstream out(path, std::ios::binary | std::ios::trunc);
            if (!out) ViewerRpcServer::fail("io_error", "cannot write " + path);
            out << args.value("source", std::string{});
            out.close();
            if (!out) ViewerRpcServer::fail("io_error", "cannot write " + path);
        } else if (args.contains("path")) {
            path = args.value("path", std::string{});
        } else {
            ViewerRpcServer::fail("bad_args", "load needs 'path' or 'source'");
        }
        g_rpcImportRoots = roots;
        if (!loadFile(path)) ViewerRpcServer::fail("io_error", "cannot open " + path);
        // Static check only (closure -> expand -> typecheck): a broken file is
        // answered in milliseconds, no Engine::run.
        std::vector<std::string> checkRoots = roots;
        const std::string dir = std::filesystem::path(path).parent_path().string();
        if (!dir.empty()) checkRoots.push_back(dir);
        json data = staticCheckJson(g_doc, checkRoots, boundParamNames());
        data["path"] = path;
        return data;
    });

    server.on("params", [](uint64_t, const json& args) -> std::optional<json> {
        json unknown = json::array();
        for (const auto& [name, val] : args.items()) {
            bool found = false;
            for (auto& [pname, ptext] : g_paramValues)
                if (pname == name) {
                    ptext = jsonToParamText(val);
                    found = true;
                }
            if (!found) unknown.push_back(name);
        }
        return json{{"params", paramsJson()}, {"unknown", unknown}};
    });

    server.on("render", [](uint64_t clientId, const json& args) -> std::optional<json> {
        if (!g_state.gfxOk || !g_state.imguiOk)
            ViewerRpcServer::fail("no_frame_loop",
                                  "render needs the frame loop and the preview pane (unavailable with "
                                  "--no-ui or in --smoke)");
        if (g_filePath.empty()) ViewerRpcServer::fail("no_file", "no .pgg file loaded");
        const std::string node = args.value("node", std::string{});
        if (node.empty()) ViewerRpcServer::fail("bad_args", "render needs 'node'");
        if (g_pendingRender.active) ViewerRpcServer::fail("busy", "a previous render is still pending");

        if (args.contains("highlight"))
            g_previewOpts.highlightGroup = args.value("highlight", std::string{});
        if (args.contains("shading")) {
            const std::string v = args.value("shading", std::string{"auto"});
            g_previewOpts.shading = v == "flat"     ? PreviewShading::Flat
                                    : v == "smooth" ? PreviewShading::Smooth
                                                    : PreviewShading::Auto;
        }
        if (args.contains("colors")) g_previewOpts.vertexColors = args.value("colors", true);
        if (args.contains("orbit")) {
            const json& o = args["orbit"];
            if (o.is_array() && o.size() >= 2) {
                const float yaw = o[0].get<float>();
                const float pitch = o[1].get<float>();
                const float zoom = o.size() >= 3 ? o[2].get<float>() : 1.0f;
                g_preview.setOrbit(yaw, pitch, zoom);
            }
        }
        // A2 camera targeting: the spec is re-applied by runPreview after the
        // rebuild. An explicit "" clears a target set by an earlier render;
        // without a "target" arg the previous one persists (like the other
        // options) — pair with fit=all to reset the framing.
        if (args.contains("target")) {
            const std::string t = args.value("target", std::string{});
            if (t.empty()) {
                g_cameraTarget = CameraTargetSpec{};
            } else {
                g_cameraTarget = parseCameraTargetSpec(t);
                if (g_cameraTarget.kind == CameraTargetSpec::Kind::None)
                    ViewerRpcServer::fail("bad_args",
                                          "unparseable target '" + t +
                                              "' (want x,y,z | group:<name> | binding:<path>)");
                if (!args.contains("fit")) g_preview.setFitMode(PreviewFitMode::Target);
            }
        }
        if (args.contains("fit")) {
            const std::string f = args.value("fit", std::string{});
            if (f == "target") {
                g_preview.setFitMode(PreviewFitMode::Target);
            } else if (f == "all") {
                g_preview.setFitMode(PreviewFitMode::All);
            } else {
                ViewerRpcServer::fail("bad_args", "fit must be 'all' or 'target'");
            }
        }
        if (args.contains("ortho")) {
            const std::string o = args.value("ortho", std::string{});
            if (o == "front") {
                g_preview.setProjection(PreviewProjection::OrthoFront);
            } else if (o == "side") {
                g_preview.setProjection(PreviewProjection::OrthoSide);
            } else if (o == "top") {
                g_preview.setProjection(PreviewProjection::OrthoTop);
            } else if (o == "off" || o == "perspective") {
                g_preview.setProjection(PreviewProjection::Perspective);
            } else {
                ViewerRpcServer::fail("bad_args", "ortho must be front|side|top|off");
            }
        }
        if (args.contains("wire")) g_preview.setWireframe(args.value("wire", false));
        if (args.contains("size")) {
            const json& sz = args["size"];
            if (sz.is_array() && sz.size() >= 2)
                g_cliPreviewSize = ImVec2(sz[0].get<float>(), sz[1].get<float>());
        }

        // Phase 1: options applied, synchronous pull (MVP: heavy graphs block
        // the frame loop, like the interactive Preview button). Phase 2 — the
        // capture + reply — happens in frame() after the first committed
        // frame with the new geometry.
        runPreview(node);
        if (!g_previewHasValue)
            ViewerRpcServer::fail("run_failed",
                                  g_lastPreviewError.empty() ? "run failed for '" + node + "'"
                                                             : g_lastPreviewError);

        std::filesystem::path out;
        const std::string outArg = args.value("out", std::string{});
        if (!outArg.empty()) {
            out = outArg;
        } else {
            out = repoRoot() / "tmp" / "pgg_rpc_shots" / ("shot_" + std::to_string(++g_shotCounter) + ".png");
        }
        std::error_code ec;
        if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path(), ec);

        g_pendingRender.active = true;
        g_pendingRender.clientId = clientId;
        g_pendingRender.outPath = out.string();
        g_pendingRender.node = node;
        g_pendingRender.stats = valueStatsJson(g_previewValue, g_lastRunMs);
        g_pendingRender.cacheHits = g_lastCacheHits;
        g_pendingRender.cacheMisses = g_lastCacheMisses;
        return std::nullopt;  // deferred reply from frame()
    });

    server.on("probe", [](uint64_t, const json& args) -> std::optional<json> {
        if (g_filePath.empty()) ViewerRpcServer::fail("no_file", "no .pgg file loaded");
        const std::string spec = args.value("spec", std::string{});
        if (spec.empty()) ViewerRpcServer::fail("bad_args", "probe needs 'spec'");
        pgg::RunParams rp;
        for (const auto& [name, text] : g_paramValues)
            if (!text.empty()) rp.values.push_back({name, parseCliValue(text)});
        rp.importRoots = g_rpcImportRoots;
        rp.cache = g_memoryCache.get();
        rp.probes = {spec};
        // Synchronous run by design (MVP), same trade-off as the Probe panel.
        const double t0 = wallNowSec();
        pgg::RunResult r = pgg::runFile(g_filePath, rp);
        const double ms = (wallNowSec() - t0) * 1000.0;
        g_lastRunMs = ms;
        g_lastCacheHits = r.stats.cacheHits;
        g_lastCacheMisses = r.stats.cacheMisses;
        json records = json::array();
        for (const pgg::ProbeRecord& pr : r.probes)
            records.push_back(
                {{"origin", pr.origin}, {"path", pr.path}, {"inspector", pr.inspector}, {"text", pr.text}});
        return json{{"records", records},
                    {"diagnostics", diagnosticsJson(r.diagnostics)},
                    {"has_errors", r.hasErrors()},
                    {"ms", ms},
                    {"cache", {{"hits", g_lastCacheHits}, {"misses", g_lastCacheMisses}}}};
    });

    server.on("export", [](uint64_t, const json& args) -> std::optional<json> {
        if (g_filePath.empty()) ViewerRpcServer::fail("no_file", "no .pgg file loaded");
        const std::string node = args.value("node", std::string{});
        const std::string objPath = args.value("obj_path", std::string{});
        if (node.empty() || objPath.empty())
            ViewerRpcServer::fail("bad_args", "export needs 'node' and 'obj_path'");
        pgg::RunParams rp;
        for (const auto& [name, text] : g_paramValues)
            if (!text.empty()) rp.values.push_back({name, parseCliValue(text)});
        rp.importRoots = g_rpcImportRoots;
        rp.cache = g_memoryCache.get();
        rp.pulls = {node};
        const double t0 = wallNowSec();
        pgg::RunResult r = pgg::runFile(g_filePath, rp);
        const double ms = (wallNowSec() - t0) * 1000.0;
        g_lastRunMs = ms;
        g_lastCacheHits = r.stats.cacheHits;
        g_lastCacheMisses = r.stats.cacheMisses;
        g_lastRunDiags = r.diagnostics;

        pgg::Value value;
        bool found = false;
        for (const pgg::RunOutput& o : r.pulled) {
            const pgg::ScalarType base = pgg::valueBase(o.value);
            if (base == pgg::ScalarType::Geo || base == pgg::ScalarType::Sdf) {
                value = o.value;
                found = true;
                break;
            }
        }
        if (!found) {
            std::string why;
            for (const pgg::Diagnostic& d : r.diagnostics)
                if (!d.isWarning) why += (why.empty() ? "" : "\n") + d.code + " " + d.message;
            ViewerRpcServer::fail("run_failed",
                                  why.empty() ? "no geometry value at '" + node + "'" : why);
        }
        if (pgg::valueBase(value) == pgg::ScalarType::Sdf)
            ViewerRpcServer::fail("no_geometry",
                                  "sdf values are not exported; mesh them with mesh_from_sdf");
        pgg::GeoPtr geo = pgg::asGeo(value);
        bool realized = false;
        if (geo->kind == pgg::GeoKind::Instances) {
            // Instances have no polygons of their own: realize first (§8.8).
            geo = pgg::realizeInstances(*geo);
            realized = true;
            if (!geo) ViewerRpcServer::fail("run_failed", "realizeInstances returned null");
            value = pgg::Value(geo);
        }
        std::error_code ec;
        const std::filesystem::path parent = std::filesystem::path(objPath).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
        std::string err;
        if (!pgg::writeObj(objPath, *geo, &err)) ViewerRpcServer::fail("io_error", err);
        return json{{"path", objPath},
                    {"realized", realized},
                    {"stats", valueStatsJson(value, ms)},
                    {"cache", {{"hits", g_lastCacheHits}, {"misses", g_lastCacheMisses}}}};
    });

    server.on("docs", [](uint64_t, const json& args) -> std::optional<json> {
        if (!g_doc.file) ViewerRpcServer::fail("no_file", "no .pgg file loaded");
        const std::string symbol = args.value("symbol", std::string{});
        if (symbol.empty()) ViewerRpcServer::fail("bad_args", "docs needs 'symbol'");
        pgg::DocsLookupResult res = pgg::findDef(*g_doc.file, g_filePath, symbol, g_rpcImportRoots);
        if (!res.found) {
            std::string msg = res.error;
            if (msg.empty())
                for (const pgg::Diagnostic& d : res.diagnostics)
                    if (!d.isWarning) msg += (msg.empty() ? "" : "\n") + d.code + " " + d.message;
            ViewerRpcServer::fail("not_found", msg.empty() ? "def not found: " + symbol : msg);
        }
        return json{{"symbol", symbol},
                    {"signature", res.signature},
                    {"docstring", res.hasDoc ? res.docstring : std::string{}}};
    });
}

int main(int argc, char* argv[]) {
    bool smoke = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg(argv[i]);
        if (arg == "--smoke") {
            smoke = true;
        } else if (arg == "--no-ui") {
            g_noUi = true;
        } else if (arg.rfind("--shot=", 0) == 0) {
            g_shotPath = arg.substr(7);
        } else if (arg.rfind("--shot-delay=", 0) == 0) {
            g_shotDelaySec = std::max(0.0, std::atof(arg.substr(13).c_str()));
            g_shotDelayExplicit = true;
        } else if (arg == "--serve") {
            g_serveAddress = "127.0.0.1:" + std::to_string(ViewerRpcServer::kDefaultPort);
        } else if (arg.rfind("--serve=", 0) == 0) {
            g_serveAddress = arg.substr(8);
        } else if (arg.rfind("--zoom=", 0) == 0) {
            g_cliZoom = static_cast<float>(std::atof(arg.substr(7).c_str()));
        } else if (arg.rfind("--center=", 0) == 0) {
            g_cliCenter = parseVec2Arg(arg.substr(9));
        } else if (arg.rfind("--dive=", 0) == 0) {
            g_cliDive = arg.substr(7);
        } else if (arg.rfind("--preview=", 0) == 0) {
            g_cliPreview = arg.substr(10);
        } else if (arg.rfind("--preview-highlight=", 0) == 0) {
            g_previewOpts.highlightGroup = arg.substr(20);
        } else if (arg.rfind("--preview-shading=", 0) == 0) {
            const std::string v = arg.substr(18);
            g_previewOpts.shading = v == "flat"     ? PreviewShading::Flat
                                    : v == "smooth" ? PreviewShading::Smooth
                                                    : PreviewShading::Auto;
        } else if (arg.rfind("--preview-colors=", 0) == 0) {
            g_previewOpts.vertexColors = arg.substr(17) != "off";
        } else if (arg.rfind("--preview-orbit=", 0) == 0) {
            float yaw = 0.0f, pitch = 0.0f, zoom = 1.0f;
            const int n = std::sscanf(arg.c_str() + 16, "%f,%f,%f", &yaw, &pitch, &zoom);
            if (n >= 2) g_cliOrbit = glm::vec3(yaw, pitch, n == 3 ? zoom : 1.0f);
        } else if (arg.rfind("--preview-target=", 0) == 0) {
            g_cliPreviewTarget = arg.substr(17);
        } else if (arg.rfind("--preview-fit=", 0) == 0) {
            g_cliPreviewFit = arg.substr(14);
        } else if (arg.rfind("--preview-ortho=", 0) == 0) {
            g_cliPreviewOrtho = arg.substr(16);
        } else if (arg.rfind("--preview-wire=", 0) == 0) {
            g_cliPreviewWire = arg.substr(15) == "on";
        } else if (arg.rfind("--preview-size=", 0) == 0) {
            float pw = 0.0f, ph = 0.0f;
            if (std::sscanf(arg.c_str() + 15, "%f,%f", &pw, &ph) == 2) g_cliPreviewSize = ImVec2(pw, ph);
        } else if (arg.rfind("--param=", 0) == 0) {
            const std::string kv = arg.substr(8);
            const size_t eq = kv.find('=');
            if (eq != std::string::npos) g_cliParams.push_back({kv.substr(0, eq), kv.substr(eq + 1)});
        } else if (arg.rfind("--", 0) != 0) {
            g_pendingLoad = arg;
        }
    }

    if (smoke) {
        spdlog::set_level(spdlog::level::info);
        return runPggViewerSmokeTest(g_serveAddress) ? 0 : 1;
    }

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1440;
    desc.height = 900;
    desc.sample_count = 1;
    desc.window_title = "PggViewer - PGG Node Projection";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = slog_func;

    sapp_run(&desc);
    return 0;
}
