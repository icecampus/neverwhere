// PggViewer: read-only node-graph projection of .pgg files (spec §10, stage E8).
//   PggViewer [file.pgg] [--shot=out.png] [--shot-delay=S] [--shot-frame=window|preview]
//             [--zoom=Z] [--center=X,Y] [--no-ui]
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
// F4: render/probe/export auto-reload the file (and its import closure) when
// an mtime of the watched files changed on disk since the last loadFile.
// F3: render{compare:"prev"} diffs the capture against the stored previous
// frame of the same view (FrameCompare.cpp); reference{image,node,...} answers
// a side-by-side PNG + silhouette metrics of the model vs a reference image.

#include "pch.h"

#include <array>
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
#include <pgg/src/eval/builtin_docs.h>  // RPC docs builtin:<name> (D3)
#include <pgg/src/eval/builtins.h>  // realizeInstances for the RPC export
#include <pgg/src/eval/cache.h>
#include <pgg/src/eval/docs_lookup.h>
#include <pgg/src/eval/expand.h>
#include <pgg/src/eval/fingerprint.h>  // fingerprintValue for the RPC diff snapshot
#include <pgg/src/eval/modules.h>
#include <pgg/src/eval/obj_export.h>
#include <pgg/src/eval/sdf.h>
#include <pgg/src/eval/typecheck.h>
#include <pgg/src/graph.h>
#include <pgg/src/layout.h>

#include "FileDialog.h"
#include "FrameCompare.h"
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

// F4 auto-reload (docs/pgg/agent_tooling_plan.md): the files the document
// state was built from — the main file (canonical path) plus the
// canonicalPath of every module of its import closure — each mapped to its
// mtime at load time. The RPC run commands (render/probe/export) stat these
// before a run and reload on any change, so an on-disk edit of the .pgg (or
// of a lib/ import) shows up without an explicit load.
std::map<std::string, std::int64_t> g_fileMtimes;
// load{source} loads a temp file under tmp/pgg_rpc_source that nothing else
// edits (the next load{source} replaces it) — mtime tracking is skipped for
// it. Cleared by loadFile, set by the RPC load handler.
bool g_mainFileFromRpcSource = false;

// File mtime as an int64 (ns of the file clock; only equality is used, so the
// epoch does not matter). An unreadable file maps to 0, which never equals a
// real recorded timestamp and therefore reads as "changed".
std::int64_t fileMtimeNs(const std::string& path) {
    std::error_code ec;
    const std::filesystem::file_time_type t = std::filesystem::last_write_time(path, ec);
    if (ec) return 0;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(t.time_since_epoch()).count();
}

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
    enum class Kind { None, Point, Group, Binding, GroupOnBinding };
    Kind kind = Kind::None;
    glm::vec3 point{0.0f};
    std::string name;     // group name ("<domain>:<name>" or bare) / binding pull path
    std::string binding;  // GroupOnBinding: the pull path after '@'
    std::string raw;      // as given (logs)
};
CameraTargetSpec g_cameraTarget;  // active target ("" = none); RPC "" clears it
// Why the last applyCameraTarget() could not resolve the target ("" = resolved
// or no target). The RPC render reports it as an error instead of silently
// shipping a fit=all picture the agent did not ask for.
std::string g_cameraTargetError;
// Auto-yaw: without an explicit orbit in the same render/CLI, a resolved target
// turns the camera to the target's side of the scene (faceTargetFromOutside).
bool g_cameraTargetAutoYaw = true;
std::string g_cliPreviewTarget;
std::string g_cliPreviewFit;    // --preview-fit=all|target ("" = auto: target iff a target is given)
std::string g_cliPreviewOrtho;  // --preview-ortho=front|side|top
bool g_cliPreviewWire = false;  // --preview-wire=on
std::map<std::string, std::pair<glm::vec3, glm::vec3>> g_previewGroupBBoxes;  // of the last build
// binding: target resolution cache: pulling it costs a run (zones are
// uncached by design), so it is resolved once per load, not per rebuild.
bool g_bindingTargetResolved = false;
std::string g_bindingTargetPath;
PreviewGeometry g_bindingTargetGeo;
glm::vec3 g_bindingTargetCenter{0.0f};
float g_bindingTargetRadius = 1.0f;
bool g_cameraTargetHasBBox = false;
glm::vec3 g_cameraTargetBMin{0.0f};
glm::vec3 g_cameraTargetBMax{0.0f};
bool g_rpcChromeOff = false;  // last RPC chrome (C5 echo); not the per-frame consume flag

struct NamedView {
    std::string name;
    nlohmann::json fields;
};
std::vector<NamedView> g_namedViews;

// --- CLI ----------------------------------------------------------------------------

std::string g_pendingLoad;
std::string g_shotPath;
double g_shotDelaySec = 1.0;  // --shot-delay=: wall time before the capture
bool g_shotDelayExplicit = false;  // --shot-delay given: keep the wall-time behaviour
bool g_shotFramePreview = false;   // --shot-frame=preview: crop the shot to the preview viewport (F1)
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
std::vector<pgg::BindingProfile> g_lastProfile;       // per-binding wall times of the last run (E)
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
    bool previewOnly = true;  // frame=preview|window (F1; RPC default preview)
    bool chromeOff = false;   // chrome=off was in effect for the captured frame
    int wantW = 0, wantH = 0; // frame=preview size arg: target crop size in px
    nlohmann::json stats;  // {kind,pts,tri,bbox,groups,ms} of the pulled value
    nlohmann::json diagnostics;  // warnings of the run (errors fail the render instead)
    bool reloaded = false;           // F4: an auto-reload preceded the run
    nlohmann::json loadDiagnostics;  // F4: load diagnostics of that reload ([] when clean)
    uint64_t cacheHits = 0, cacheMisses = 0;
    // F3 compare=prev: the reply gains diff metrics against the stored frame
    // of the same view key (built at phase 1 from the effective state).
    bool comparePrev = false;
    bool compareBaseline = false;
    bool saveBaseline = false;
    bool writePng = true;
    nlohmann::json renderState;
    std::string frameKey;
    // F3 reference command: same two-phase pipeline, but the capture stays in
    // memory (no model-only PNG) and the reply carries the side-by-side PNG +
    // silhouette metrics of the model vs this stashed reference image.
    bool isReference = false;
    std::string refImagePath;
    std::vector<std::uint8_t> refPixels;
    int refW = 0, refH = 0;
};
PendingRender g_pendingRender;

// F3: the single stored frame compare=prev diffs against. Replaced by every
// successful render (with or without compare — the "previous frame of this
// view" is then the most recent one); the reference command does not touch it.
// One slot by design (~8 MB per frame).
std::string g_lastFrameKey;
int g_lastFrameW = 0, g_lastFrameH = 0;
std::vector<std::uint8_t> g_lastFramePixels;
uint64_t g_diffCounter = 0;  // diff_N.png numbering (compare=prev)
uint64_t g_refCounter = 0;   // ref_N.png numbering (reference)

struct BaselineFrame {
    int w = 0, h = 0;
    std::vector<std::uint8_t> pixels;
};
std::map<std::string, BaselineFrame> g_baselineFrames;

// RPC diff (agent_tooling_plan C2, the server half): the snapshot of the
// outputs' structural fingerprints the diff command compares against.
// Created by the first `diff` call (answer baseline_created) or by
// `load {snapshot:true}`; refreshed ONLY by `diff {update:true}` or an
// explicit `load` (which clears it — a new document context). The F4
// auto-reload inside the run commands does NOT clear it: catching exactly
// that on-disk edit is what diff is for.
struct DiffSnapshot {
    std::string filePath;  // the document the snapshot belongs to
    // output name -> fingerprint (nullopt = sdf/field, no structural hash), in
    // output order.
    std::vector<std::pair<std::string, std::optional<uint64_t>>> fps;
};
std::optional<DiffSnapshot> g_diffSnapshot;

// chrome=off (F1): the RPC render handler asks for one frame without the side
// panel and the graph — the preview pane spans the whole window. Set during
// the poll phase, consumed by the drawing section of the same frame().
bool g_chromeOffThisFrame = false;

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

void loadNamedViews(const std::string& pggPath) {
    g_namedViews.clear();
    const std::filesystem::path p(pggPath);
    const std::filesystem::path viewsPath = p.parent_path() / (p.stem().string() + ".views.json");
    std::ifstream in(viewsPath, std::ios::binary);
    if (!in) return;
    nlohmann::json j;
    try {
        in >> j;
    } catch (const std::exception& e) {
        spdlog::warn("PggViewer: cannot parse {}: {}", viewsPath.string(), e.what());
        return;
    }
    if (!j.is_array()) {
        spdlog::warn("PggViewer: {} must be a JSON array of view objects", viewsPath.string());
        return;
    }
    for (const nlohmann::json& item : j) {
        if (!item.is_object()) continue;
        const std::string name = item.value("name", std::string{});
        if (name.empty()) continue;
        NamedView v;
        v.name = name;
        v.fields = item;
        g_namedViews.push_back(std::move(v));
    }
    spdlog::info("PggViewer: loaded {} named view(s) from {}", g_namedViews.size(), viewsPath.string());
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
    g_bindingTargetPath.clear();
    g_bindingTargetGeo = PreviewGeometry{};
    g_cameraTargetHasBBox = false;
    g_namedViews.clear();
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
    // F4: remember what was loaded (main file + every module of the import
    // closure) for the auto-reload check of the RPC run commands. A load
    // that failed to even open the file returns earlier and keeps the
    // previous watch set — the intact state still belongs to it.
    g_mainFileFromRpcSource = false;
    g_fileMtimes.clear();
    {
        std::error_code ec;
        const std::filesystem::path canon = std::filesystem::weakly_canonical(std::filesystem::path(path), ec);
        const std::string mainKey = ec ? path : canon.string();
        g_fileMtimes[mainKey] = fileMtimeNs(mainKey);
        if (g_closure)
            for (const pgg::ModuleInfo* m : g_closure->modules)
                g_fileMtimes[m->canonicalPath] = fileMtimeNs(m->canonicalPath);
    }
    loadNamedViews(path);
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
    rp.profile = true;  // E: status carries the last run's per-binding times
    rp.probes = {target + ":" + inspector};
    // Synchronous run by design (MVP): heavy graphs block the UI for seconds.
    pgg::RunResult r = pgg::runFile(g_filePath, rp);
    g_lastCacheHits = r.stats.cacheHits;
    g_lastCacheMisses = r.stats.cacheMisses;
    g_lastProfile = r.stats.profile;
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

std::string slashToDot(std::string path) {
    for (char& c : path)
        if (c == '/') c = '.';
    return path;
}

CameraTargetSpec parseCameraTargetSpec(const std::string& text) {
    CameraTargetSpec spec;
    spec.raw = text;
    if (text.rfind("group:", 0) == 0) {
        const std::string rest = text.substr(6);
        const size_t at = rest.find('@');
        if (at != std::string::npos) {
            spec.kind = CameraTargetSpec::Kind::GroupOnBinding;
            spec.name = rest.substr(0, at);
            spec.binding = slashToDot(rest.substr(at + 1));
        } else {
            spec.kind = CameraTargetSpec::Kind::Group;
            spec.name = rest;
        }
    } else if (text.rfind("binding:", 0) == 0) {
        spec.kind = CameraTargetSpec::Kind::Binding;
        spec.name = slashToDot(text.substr(8));
    } else {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (std::sscanf(text.c_str(), "%f,%f,%f", &x, &y, &z) == 3) {
            spec.kind = CameraTargetSpec::Kind::Point;
            spec.point = glm::vec3(x, y, z);
        }
    }
    if (spec.kind == CameraTargetSpec::Kind::GroupOnBinding &&
        (spec.name.empty() || spec.binding.empty()))
        spec.kind = CameraTargetSpec::Kind::None;
    if (spec.kind != CameraTargetSpec::Kind::Point && spec.kind != CameraTargetSpec::Kind::GroupOnBinding &&
        spec.name.empty())
        spec.kind = CameraTargetSpec::Kind::None;
    return spec;
}

// Resolves a group target against the last build's per-group bboxes: exact
// "<domain>:<name>" first, then a bare-name match (an ambiguous bare name
// takes the first sorted key and logs the ambiguity).
bool resolveGroupBBox(const std::map<std::string, std::pair<glm::vec3, glm::vec3>>& boxes,
                      const std::string& name, glm::vec3& outMin, glm::vec3& outMax) {
    auto found = boxes.end();
    if (const auto it = boxes.find(name); it != boxes.end()) {
        found = it;
    } else {
        for (auto jt = boxes.begin(); jt != boxes.end(); ++jt) {
            const size_t colon = jt->first.find(':');
            const std::string bare = colon == std::string::npos ? jt->first : jt->first.substr(colon + 1);
            if (bare != name) continue;
            if (found != boxes.end())
                spdlog::warn("PggViewer: target group '{}' is ambiguous (taking '{}', also '{}')", name,
                             found->first, jt->first);
            else
                found = jt;
        }
    }
    if (found == boxes.end()) {
        std::string known;
        for (const auto& [key, bb] : boxes) known += (known.empty() ? "" : ", ") + key;
        g_cameraTargetError = "group '" + name + "' is not on the geometry (groups: " +
                              (known.empty() ? std::string("none") : known) + ")";
        return false;
    }
    outMin = found->second.first;
    outMax = found->second.second;
    return true;
}

bool resolveGroupTarget(const std::string& name, glm::vec3& outCenter, float& outRadius) {
    glm::vec3 mn, mx;
    if (!resolveGroupBBox(g_previewGroupBBoxes, name, mn, mx)) return false;
    outCenter = (mn + mx) * 0.5f;
    outRadius = glm::length(mx - mn) * 0.5f;
    g_cameraTargetBMin = mn;
    g_cameraTargetBMax = mx;
    g_cameraTargetHasBBox = true;
    return true;
}

// Pulls the binding of a binding: camera target and takes its bbox (sdf is
// meshed at the default preview voxel). Resolved once per load — zones are
// uncached by design, so re-pulling on every rebuild would repeat the cost.
bool pullBindingPreview(const std::string& path) {
    if (g_bindingTargetResolved && g_bindingTargetPath == path) return true;
    g_bindingTargetResolved = false;
    g_bindingTargetPath = path;
    pgg::RunParams rp;
    for (const auto& [name, text] : g_paramValues)
        if (!text.empty()) rp.values.push_back({name, parseCliValue(text)});
    rp.importRoots = g_rpcImportRoots;
    rp.cache = g_memoryCache.get();
    rp.profile = true;
    rp.pulls = {path};
    pgg::RunResult r = pgg::runFile(g_filePath, rp);
    g_lastProfile = r.stats.profile;
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
        std::string why;
        for (const pgg::Diagnostic& d : r.diagnostics)
            if (!d.isWarning && d.code == "E606") why = d.message;
        g_cameraTargetError = "binding '" + path + "' gave no geometry value" +
                              (why.empty() ? std::string(" (a pull names a top-level binding, an instance or "
                                                         "<instance>.<local>; locals of an inlined def are "
                                                         "not addressable)")
                                           : " (" + why + ")");
        spdlog::warn("PggViewer: preview-target {}", g_cameraTargetError);
        return false;
    }
    g_bindingTargetGeo = buildPreviewGeometry(value, PreviewBuildOptions{});
    if (!g_bindingTargetGeo.ok) {
        g_cameraTargetError = "binding '" + path + "' has nothing to bound (" + g_bindingTargetGeo.summary + ")";
        spdlog::warn("PggViewer: preview-target {}", g_cameraTargetError);
        return false;
    }
    g_bindingTargetCenter = (g_bindingTargetGeo.bmin + g_bindingTargetGeo.bmax) * 0.5f;
    g_bindingTargetRadius = std::max(1e-3f, glm::length(g_bindingTargetGeo.bmax - g_bindingTargetGeo.bmin) * 0.5f);
    g_bindingTargetResolved = true;
    return true;
}

bool resolveBindingTarget(const std::string& path, glm::vec3& outCenter, float& outRadius) {
    if (!pullBindingPreview(path)) return false;
    outCenter = g_bindingTargetCenter;
    outRadius = g_bindingTargetRadius;
    g_cameraTargetBMin = g_bindingTargetGeo.bmin;
    g_cameraTargetBMax = g_bindingTargetGeo.bmax;
    g_cameraTargetHasBBox = true;
    return true;
}

bool resolveGroupOnBinding(const std::string& group, const std::string& path, glm::vec3& outCenter,
                           float& outRadius) {
    if (!pullBindingPreview(path)) return false;
    glm::vec3 mn, mx;
    if (!resolveGroupBBox(g_bindingTargetGeo.groupBBoxes, group, mn, mx)) {
        g_cameraTargetError = "group '" + group + "' is not on binding '" + path +
                              "' (groups: " + [&] {
                                  std::string known;
                                  for (const auto& [key, bb] : g_bindingTargetGeo.groupBBoxes)
                                      known += (known.empty() ? "" : ", ") + key;
                                  return known.empty() ? std::string("none") : known;
                              }() +
                              ")";
        return false;
    }
    outCenter = (mn + mx) * 0.5f;
    outRadius = glm::length(mx - mn) * 0.5f;
    g_cameraTargetBMin = mn;
    g_cameraTargetBMax = mx;
    g_cameraTargetHasBBox = true;
    return true;
}

void rememberPointBBox(const glm::vec3& p) {
    g_cameraTargetBMin = p;
    g_cameraTargetBMax = p;
    g_cameraTargetHasBBox = true;
}

// Re-applies the CLI/RPC camera target after a preview (re)build — the target
// must survive runPreview's refit. An unresolvable target falls back to
// fit=all with a warning.
void applyCameraTarget() {
    g_cameraTargetError.clear();
    g_cameraTargetHasBBox = false;
    if (g_cameraTarget.kind == CameraTargetSpec::Kind::None) return;
    glm::vec3 center{0.0f};
    float radius = 1.0f;
    bool ok = false;
    switch (g_cameraTarget.kind) {
        case CameraTargetSpec::Kind::Point:
            center = g_cameraTarget.point;
            radius = g_preview.sceneRadius();
            rememberPointBBox(center);
            ok = true;
            break;
        case CameraTargetSpec::Kind::Group:
            ok = resolveGroupTarget(g_cameraTarget.name, center, radius);
            break;
        case CameraTargetSpec::Kind::Binding:
            ok = resolveBindingTarget(g_cameraTarget.name, center, radius);
            break;
        case CameraTargetSpec::Kind::GroupOnBinding:
            ok = resolveGroupOnBinding(g_cameraTarget.name, g_cameraTarget.binding, center, radius);
            break;
        default:
            break;
    }
    if (ok) {
        g_preview.setTarget(center, radius);
        if (g_cameraTargetAutoYaw) g_preview.faceTargetFromOutside();
    } else {
        if (g_cameraTargetError.empty()) g_cameraTargetError = "target '" + g_cameraTarget.raw + "' not resolved";
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
    rp.profile = true;
    rp.pulls = {target};
    const uint64_t t0 = stm_now();
    pgg::RunResult r = pgg::runFile(g_filePath, rp);
    const double ms = stm_ms(stm_diff(stm_now(), t0));
    g_lastRunMs = ms;
    g_lastCacheHits = r.stats.cacheHits;
    g_lastCacheMisses = r.stats.cacheMisses;
    g_lastProfile = r.stats.profile;
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

// The ##preview window at an explicit rect (shared by the docked pane and the
// chrome=off full-window frame, F1). drawWindowContents records the image
// rect in framebuffer pixels for screenshot crops.
void drawPreviewWindowAt(float x, float y, float w, float h) {
    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::Begin("##preview", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBringToFrontOnFocus);
    g_preview.drawWindowContents();
    ImGui::End();
}

// Docked preview pane under the splitter.
void drawPreviewPane(int w, int h, float graphH) {
    if (!g_showPreview) return;
    const float x0 = panelWidth();
    drawPreviewWindowAt(x0, graphH + kSplitterHeight, static_cast<float>(w) - x0,
                        static_cast<float>(h) - graphH - kSplitterHeight);
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
// Every backend normalizes to a TOP-DOWN RGBA8 buffer (GL's bottom-up rows
// are flipped right after the readback), so the F1 crop works in screen
// coordinates on all backends.
// The current frame's swapchain descriptor, stashed by frame() for capturePng
// (Metal drawable / D3D11 render view; only valid during the frame callback).
sg_swapchain g_frameSwapchain = {};

void swizzleBgraToRgba(std::vector<std::uint8_t>& pixels) {
    for (std::size_t i = 0; i + 3 < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
}

void flipVertically(std::vector<std::uint8_t>& pixels, int width, int height) {
    std::vector<std::uint8_t> row(static_cast<std::size_t>(width) * 4);
    for (int y = 0; y < height / 2; ++y) {
        std::uint8_t* top = pixels.data() + static_cast<std::size_t>(y) * width * 4;
        std::uint8_t* bot = pixels.data() + static_cast<std::size_t>(height - 1 - y) * width * 4;
        std::memcpy(row.data(), top, row.size());
        std::memcpy(top, bot, row.size());
        std::memcpy(bot, row.data(), row.size());
    }
}

bool writePng(const char* path, int width, int height, const std::vector<std::uint8_t>& pixels) {
    const int ok = stbi_write_png(path, width, height, 4, pixels.data(), width * 4);
    if (!ok) {
        spdlog::error("capturePng: stbi_write_png failed for {}", path);
        return false;
    }
    return true;
}

// What the capture keeps (F1): the whole window, or the preview viewport rect
// (GeometryPreview::lastImageRectPx, refreshed every frame). wantW/wantH > 0
// ask for a target crop size in pixels, centered on the viewport; the window
// is never resized (sokol cannot do that on every backend), so a larger
// request clamps to the actual rect and the reply says size_clamped.
struct ShotCrop {
    bool previewOnly = false;
    int wantW = 0, wantH = 0;
};

struct CaptureResult {
    bool ok = false;
    int width = 0, height = 0;  // of the written PNG (framebuffer px)
    bool sizeClamped = false;   // the requested crop size exceeded the viewport rect
    // The final post-crop top-down RGBA8 buffer, moved out (F3 compare/
    // reference consume it in memory). Empty when !ok.
    std::vector<std::uint8_t> pixels;
};

// An empty `path` skips the PNG write and only fills res.pixels (F3
// reference — the model frame goes into the side-by-side compose, not to its
// own file).
CaptureResult capturePng(const char* path, const ShotCrop& crop) {
    CaptureResult res;
    const int width = sapp_width();
    const int height = sapp_height();
    if (width <= 0 || height <= 0) return res;
    std::vector<std::uint8_t> pixels;  // top-down RGBA on every backend
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    pixels.resize(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    flipVertically(pixels, width, height);  // GL rows come bottom-up
#elif defined(SOKOL_METAL) && defined(__APPLE__)
    // Valid only inside frame() (the drawable lives in sokol_app's per-frame
    // autorelease pool); capturePng is called from frame() right after the
    // stash, same pool.
    id<CAMetalDrawable> drawable = (__bridge id<CAMetalDrawable>)g_frameSwapchain.metal.current_drawable;
    if (drawable == nil) {
        spdlog::error("capturePng: no Metal drawable in the current frame");
        return res;
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
        return res;
    }
    pixels.resize(static_cast<std::size_t>(width) * height * 4);
    [dst getBytes:pixels.data()
      bytesPerRow:static_cast<NSUInteger>(width * 4)
       fromRegion:MTLRegionMake2D(0, 0, static_cast<NSUInteger>(width), static_cast<NSUInteger>(height))
      mipmapLevel:0];
    swizzleBgraToRgba(pixels);  // CAMetalLayer is BGRA8; row 0 is the top — no flip
#elif defined(SOKOL_D3D11)
    // NOTE: written without a Windows machine at hand — verify on first use.
    // Ordering is free: CopyResource on the same immediate context is
    // serialized after the frame's commands, and Map blocks until done.
    ID3D11RenderTargetView* rtv = static_cast<ID3D11RenderTargetView*>(g_frameSwapchain.d3d11.render_view);
    if (rtv == nullptr) {
        spdlog::error("capturePng: no D3D11 render view in the current frame");
        return res;
    }
    ID3D11Resource* res11 = nullptr;
    rtv->GetResource(&res11);
    ID3D11Texture2D* backbuffer = nullptr;
    HRESULT hr = res11 != nullptr ? res11->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backbuffer))
                                  : E_POINTER;
    if (res11 != nullptr) res11->Release();
    if (FAILED(hr) || backbuffer == nullptr) {
        spdlog::error("capturePng: D3D11 backbuffer QueryInterface failed");
        return res;
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
        return res;
    }
    context->CopyResource(staging, backbuffer);
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    hr = context->Map(staging, 0, D3D11_MAP_READ, 0, &mapped);
    if (SUCCEEDED(hr)) {
        pixels.resize(static_cast<std::size_t>(width) * height * 4);
        const auto* srcRow = static_cast<const std::uint8_t*>(mapped.pData);
        for (int y = 0; y < height; ++y) {
            std::memcpy(pixels.data() + static_cast<std::size_t>(y) * width * 4, srcRow + static_cast<std::size_t>(y) * mapped.RowPitch, static_cast<std::size_t>(width) * 4);
        }
        context->Unmap(staging, 0);
        swizzleBgraToRgba(pixels);  // backbuffer is B8G8R8A8; row 0 is the top — no flip
    } else {
        spdlog::error("capturePng: D3D11 Map failed");
    }
    staging->Release();
    context->Release();
    device->Release();
    backbuffer->Release();
    if (pixels.empty()) return res;
#else
    spdlog::error("capturePng: --shot is not implemented for this backend");
    return res;
#endif

    // Common tail: F1 crop (the buffer is top-down on every backend, in
    // framebuffer pixels — same space as GeometryPreview::lastImageRectPx).
    int outW = width, outH = height;
    std::vector<std::uint8_t> cropped;
    if (crop.previewOnly) {
        const GeometryPreview::ImageRectPx rect = g_preview.lastImageRectPx();
        int cx = rect.x, cy = rect.y, cw = rect.w, ch = rect.h;
        if (crop.wantW > 0 && crop.wantH > 0) {
            res.sizeClamped = crop.wantW > rect.w || crop.wantH > rect.h;
            cw = std::min(crop.wantW, rect.w);
            ch = std::min(crop.wantH, rect.h);
            cx = rect.x + (rect.w - cw) / 2;
            cy = rect.y + (rect.h - ch) / 2;
        }
        if (!cropShotPixels(pixels, width, height, cx, cy, cw, ch, cropped, outW, outH)) {
            spdlog::error("capturePng: empty preview crop ({}x{} at {},{})", cw, ch, cx, cy);
            return res;
        }
    }
    const std::vector<std::uint8_t>& finalPixels = crop.previewOnly ? cropped : pixels;
    res.ok = true;
    if (path && path[0] != '\0') res.ok = writePng(path, outW, outH, finalPixels);
    if (res.ok) {
        res.width = outW;
        res.height = outH;
        res.pixels = crop.previewOnly ? std::move(cropped) : std::move(pixels);
    }
    return res;
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

// F4: true when any file the current document state was built from changed
// on disk since the last loadFile (mtime mismatch or unreadable).
bool watchedFilesChanged() {
    for (const auto& [path, mtime] : g_fileMtimes)
        if (fileMtimeNs(path) != mtime) return true;
    return false;
}

struct AutoReloadResult {
    bool reloaded = false;
    std::vector<pgg::Diagnostic> diags;  // load diagnostics of the reload (warnings may ride along)
};

// F4 auto-reload: if the loaded file or one of its imports changed on disk,
// reload before the run — the explicit load the agent forgot (the graph, the
// diagnostics panel and the param set all rebuild). Skipped for load{source}
// temp files. A reload with load errors throws RpcError("run_errors") — the
// same envelope as a run-time error (b3383a1) — and the caller's run never
// happens: a broken file must not produce half a scene.
AutoReloadResult autoReloadIfChanged() {
    AutoReloadResult out;
    if (g_mainFileFromRpcSource || g_filePath.empty() || g_fileMtimes.empty()) return out;
    if (!watchedFilesChanged()) return out;
    spdlog::info("PggViewer: auto-reload {} (a watched file changed on disk)", g_filePath);
    if (!loadFile(g_filePath))
        ViewerRpcServer::fail("io_error", "auto-reload: cannot open " + g_filePath);
    out.reloaded = true;
    out.diags = g_allDiags;
    if (diagsHaveErrors(g_allDiags)) {
        std::string why = "auto-reload of " + g_filePath + " has errors (run skipped):";
        for (const pgg::Diagnostic& d : g_allDiags)
            if (!d.isWarning) why += "\n" + pgg::formatDiagnostic(d, g_filePath);
        ViewerRpcServer::fail("run_errors", why);
    }
    return out;
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

nlohmann::json cameraJson() {
    nlohmann::json cam = {{"center", vec3Json(g_preview.center())},
                          {"radius", g_preview.fitRadius()},
                          {"distance", g_preview.distance()}};
    if (g_cameraTargetHasBBox) {
        const glm::vec3 c = (g_cameraTargetBMin + g_cameraTargetBMax) * 0.5f;
        cam["target_bbox"] = {{"min", vec3Json(g_cameraTargetBMin)},
                              {"max", vec3Json(g_cameraTargetBMax)},
                              {"center", vec3Json(c)}};
    }
    return cam;
}

nlohmann::json mergeNamedViewArgs(const nlohmann::json& args, std::string& err) {
    nlohmann::json out = args;
    if (!args.contains("view")) return out;
    const std::string name = args.value("view", std::string{});
    const NamedView* found = nullptr;
    for (const NamedView& v : g_namedViews)
        if (v.name == name) {
            found = &v;
            break;
        }
    if (!found) {
        std::string known;
        for (const NamedView& v : g_namedViews) known += (known.empty() ? "" : ", ") + v.name;
        err = "unknown view '" + name + "' (views: " + (known.empty() ? std::string("none") : known) + ")";
        return {};
    }
    for (auto it = found->fields.begin(); it != found->fields.end(); ++it) {
        if (it.key() == "name") continue;
        if (!args.contains(it.key()) || args[it.key()].is_null()) out[it.key()] = it.value();
    }
    out.erase("view");
    return out;
}

// F3 frame key (compare=prev): the EFFECTIVE view state — node + frame/chrome
// + camera + preview options — so an arg-less repeat render of the same view
// hits the stored frame, while any option that changes the picture misses it.
// Launch params are deliberately NOT part of the key: a params/seed edit
// followed by the same render is exactly what compare=prev exists to show.
std::string currentFrameKey(const std::string& node, bool previewOnly, bool chromeOff) {
    const nlohmann::json k = {{"node", node},
                              {"frame", previewOnly ? "preview" : "window"},
                              {"chrome", chromeOff ? "off" : "on"},
                              {"center", vec3Json(g_preview.center())},
                              {"radius", g_preview.fitRadius()},
                              {"distance", g_preview.distance()},
                              {"yaw", g_preview.yawDeg()},
                              {"pitch", g_preview.pitchDeg()},
                              {"projection", static_cast<int>(g_preview.projection())},
                              {"fit", g_preview.fitMode() == PreviewFitMode::Target ? "target" : "all"},
                              {"has_target", g_preview.hasTarget()},
                              {"highlight", g_previewOpts.highlightGroup},
                              {"shading", static_cast<int>(g_previewOpts.shading)},
                              {"colors", g_previewOpts.vertexColors},
                              {"sdf_res", g_previewOpts.sdfResolution},
                              {"wire", g_preview.wireframe()}};
    return k.dump();
}

nlohmann::json silhouetteJson(const SilhouetteMetrics& m) {
    return {{"bbox_frac", {m.bboxX0, m.bboxY0, m.bboxX1, m.bboxY1}},
            {"w_over_h", m.wOverH},
            {"rows", m.rows},
            {"empty", m.empty}};
}

// The known exact background of preview captures (the offscreen pass clear
// color) in RGBA8 — the bg for the model silhouette (F3).
std::array<std::uint8_t, 3> previewClearRgb8() {
    std::array<std::uint8_t, 3> bg{};
    for (int i = 0; i < 3; ++i)
        bg[i] = static_cast<std::uint8_t>(
            std::clamp<long>(std::lround(GeometryPreview::kClearColor[i] * 255.0f), 0, 255));
    return bg;
}

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
    g_cameraTargetAutoYaw = !g_cliOrbit.has_value();
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
        // chrome=off (F1): the RPC render handler asked for one frame without
        // the side panel and the graph — the preview pane spans the whole
        // window, so the captured crop carries only the 3D preview.
        const bool chromeOff = g_chromeOffThisFrame;
        g_chromeOffThisFrame = false;
        if (chromeOff) {
            drawPreviewWindowAt(0.0f, 0.0f, static_cast<float>(w), static_cast<float>(h));
        } else {
            drawPanel(w, h);
            drawCanvasWindow(w, graphH);
            updateAutoPreview();
            drawPreviewPane(w, h, graphH);
            if (g_showPreview) drawSplitter(w, h, graphH);
        }
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
        ShotCrop crop;
        crop.previewOnly = pr.previewOnly;
        crop.wantW = pr.wantW;
        crop.wantH = pr.wantH;
        if (pr.isReference) {
            // F3 reference: the model frame stays in memory; the reply carries
            // the side-by-side PNG (model left, reference right) + silhouette
            // metrics of both halves.
            const CaptureResult cap = capturePng(nullptr, crop);
            if (!cap.ok) {
                if (g_rpc) g_rpc->replyError(pr.clientId, "capture_failed", "capturePng failed (reference)");
                spdlog::error("PggViewer: RPC reference capture failed");
            } else {
                const auto bg = previewClearRgb8();
                const SilhouetteMetrics modelM =
                    silhouetteMetrics(cap.pixels, cap.width, cap.height, bg[0], bg[1], bg[2]);
                const auto refBg = estimateBackground(pr.refPixels, pr.refW, pr.refH);
                const SilhouetteMetrics refM =
                    silhouetteMetrics(pr.refPixels, pr.refW, pr.refH, refBg[0], refBg[1], refBg[2]);
                const SideBySideImage sbs =
                    composeSideBySide(cap.pixels, cap.width, cap.height, pr.refPixels, pr.refW, pr.refH);
                if (!sbs.ok || !writePng(pr.outPath.c_str(), sbs.width, sbs.height, sbs.pixels)) {
                    if (g_rpc)
                        g_rpc->replyError(pr.clientId, "capture_failed",
                                          "side-by-side compose/write failed for " + pr.outPath);
                    spdlog::error("PggViewer: RPC reference compose/write failed ({})", pr.outPath);
                } else {
                    nlohmann::json data = {{"path", pr.outPath},
                                           {"width", sbs.width},
                                           {"height", sbs.height},
                                           {"node", pr.node},
                                           {"model", silhouetteJson(modelM)},
                                           {"reference", silhouetteJson(refM)},
                                           {"reference_background", refBg},
                                           {"ms", pr.stats.value("ms", 0.0)},
                                           {"stats", pr.stats},
                                           {"diagnostics", pr.diagnostics},
                                           {"camera",
                                            {{"center", vec3Json(g_preview.center())},
                                             {"radius", g_preview.fitRadius()},
                                             {"distance", g_preview.distance()}}},
                                           {"cache", {{"hits", pr.cacheHits}, {"misses", pr.cacheMisses}}}};
                    if (cap.sizeClamped) data["size_clamped"] = true;
                    data["reloaded"] = pr.reloaded;
                    if (pr.reloaded) data["load_diagnostics"] = pr.loadDiagnostics;
                    if (g_rpc) g_rpc->reply(pr.clientId, data);
                    spdlog::info("PggViewer: RPC reference {} vs {} -> {}", pr.node, pr.refImagePath,
                                 pr.outPath);
                }
            }
        } else {
            const char* pngPath = pr.writePng && !pr.outPath.empty() ? pr.outPath.c_str() : "";
            const CaptureResult cap = capturePng(pngPath, crop);
            if (cap.ok) {
                nlohmann::json data = {{"width", cap.width},
                                       {"height", cap.height},
                                       {"node", pr.node},
                                       {"frame", pr.previewOnly ? "preview" : "window"},
                                       {"chrome", pr.chromeOff ? "off" : "on"},
                                       {"stats", pr.stats},
                                       {"diagnostics", pr.diagnostics},
                                       {"camera", cameraJson()},
                                       {"render_state", pr.renderState.is_null() ? pggViewerRenderStateJson()
                                                                                 : pr.renderState},
                                       {"cache", {{"hits", pr.cacheHits}, {"misses", pr.cacheMisses}}}};
                if (pr.writePng && !pr.outPath.empty()) data["path"] = pr.outPath;
                if (cap.sizeClamped) data["size_clamped"] = true;
                data["reloaded"] = pr.reloaded;
                if (pr.reloaded) data["load_diagnostics"] = pr.loadDiagnostics;

                auto packCompare = [&](const std::vector<std::uint8_t>& prev, int prevW, int prevH,
                                       const std::string& missing) -> nlohmann::json {
                    nlohmann::json cmp;
                    if (prev.empty()) {
                        cmp = {{"available", false}, {"reason", missing}};
                    } else if (prevW != cap.width || prevH != cap.height) {
                        cmp = {{"available", false},
                               {"reason", "size mismatch (prev " + std::to_string(prevW) + "x" +
                                              std::to_string(prevH) + ", now " +
                                              std::to_string(cap.width) + "x" + std::to_string(cap.height) +
                                              ")"}};
                    } else {
                        const FrameCompareResult d = compareFrames(prev, cap.pixels, cap.width, cap.height);
                        cmp = {{"available", true},
                               {"changed_pct", d.changedPct},
                               {"change_bbox_px", {d.changeX0, d.changeY0, d.changeX1, d.changeY1}}};
                        const std::filesystem::path diffPath =
                            repoRoot() / "tmp" / "pgg_rpc_shots" /
                            ("diff_" + std::to_string(++g_diffCounter) + ".png");
                        std::error_code ec;
                        std::filesystem::create_directories(diffPath.parent_path(), ec);
                        if (writePng(diffPath.string().c_str(), cap.width, cap.height, d.diffPixels)) {
                            cmp["diff_png"] = diffPath.string();
                        } else {
                            cmp["diff_png"] = nullptr;
                        }
                    }
                    return cmp;
                };

                if (pr.comparePrev) {
                    if (g_lastFrameKey != pr.frameKey)
                        data["compare"] = packCompare({}, 0, 0, "no previous frame");
                    else
                        data["compare"] =
                            packCompare(g_lastFramePixels, g_lastFrameW, g_lastFrameH, "no previous frame");
                }
                if (pr.compareBaseline) {
                    const auto it = g_baselineFrames.find(pr.frameKey);
                    if (it == g_baselineFrames.end()) {
                        data["compare"] = {{"available", false},
                                           {"reason", "baseline created"},
                                           {"baseline_created", true}};
                        BaselineFrame bf;
                        bf.w = cap.width;
                        bf.h = cap.height;
                        bf.pixels = cap.pixels;
                        g_baselineFrames[pr.frameKey] = std::move(bf);
                    } else {
                        data["compare"] = packCompare(it->second.pixels, it->second.w, it->second.h,
                                                      "no baseline frame");
                    }
                }
                if (pr.saveBaseline) {
                    BaselineFrame bf;
                    bf.w = cap.width;
                    bf.h = cap.height;
                    bf.pixels = cap.pixels;
                    g_baselineFrames[pr.frameKey] = std::move(bf);
                    data["baseline_saved"] = true;
                }

                g_lastFrameKey = pr.frameKey;
                g_lastFrameW = cap.width;
                g_lastFrameH = cap.height;
                g_lastFramePixels = cap.pixels;
                if (g_rpc) g_rpc->reply(pr.clientId, data);
                spdlog::info("PggViewer: RPC render {} -> {}", pr.node,
                             pr.writePng ? pr.outPath : std::string("(png=false)"));
            } else {
                if (g_rpc)
                    g_rpc->replyError(pr.clientId, "capture_failed",
                                      pr.writePng ? "capturePng failed for " + pr.outPath
                                                  : "capturePng failed");
                spdlog::error("PggViewer: RPC render capture failed ({})", pr.outPath);
            }
        }
    }

    // Headless capture: the graph is laid out at load time, so a short
    // wall-time settle is enough before grabbing the framebuffer. A1: with
    // --preview and no explicit --shot-delay the first committed frame after
    // the (synchronous) preview run is enough; an explicit --shot-delay keeps
    // the old wall-time behaviour. F1: --shot-frame=preview crops to the
    // preview viewport (default window = the whole frame).
    if (!g_shotPath.empty()) {
        const bool ready = g_shotDelayExplicit || g_cliPreview.empty()
                               ? stm_sec(stm_now()) >= g_shotDelaySec
                               : g_framesSincePreviewRun >= 1;
        if (ready) {
            ShotCrop crop;
            crop.previewOnly = g_shotFramePreview;
            const CaptureResult cap = capturePng(g_shotPath.c_str(), crop);
            if (cap.ok) {
                spdlog::info("PggViewer: screenshot saved to {} ({}x{})", g_shotPath, cap.width, cap.height);
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

// F1 screenshot crop (declared in SmokeTest.h so the smoke test can drive it).
// The rect is in top-down framebuffer pixels — the orientation capturePng
// normalizes every backend's readback to before calling this.
bool cropShotPixels(const std::vector<std::uint8_t>& src, int srcW, int srcH, int x, int y, int w, int h,
                    std::vector<std::uint8_t>& out, int& outW, int& outH) {
    out.clear();
    outW = outH = 0;
    if (srcW <= 0 || srcH <= 0 || src.size() < static_cast<std::size_t>(srcW) * srcH * 4) return false;
    const int x0 = static_cast<int>(std::clamp<int64_t>(x, 0, srcW));
    const int y0 = static_cast<int>(std::clamp<int64_t>(y, 0, srcH));
    const int x1 = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(x) + w, 0, srcW));
    const int y1 = static_cast<int>(std::clamp<int64_t>(static_cast<int64_t>(y) + h, 0, srcH));
    if (x1 <= x0 || y1 <= y0) return false;
    outW = x1 - x0;
    outH = y1 - y0;
    out.resize(static_cast<std::size_t>(outW) * outH * 4);
    for (int row = 0; row < outH; ++row)
        std::memcpy(out.data() + static_cast<std::size_t>(row) * outW * 4,
                    src.data() + (static_cast<std::size_t>(y0 + row) * srcW + x0) * 4,
                    static_cast<std::size_t>(outW) * 4);
    return true;
}

nlohmann::json pggViewerRenderStateJson() {
    std::string ortho = "off";
    switch (g_preview.projection()) {
        case PreviewProjection::OrthoFront: ortho = "front"; break;
        case PreviewProjection::OrthoSide: ortho = "side"; break;
        case PreviewProjection::OrthoTop: ortho = "top"; break;
        default: break;
    }
    return {{"wire", g_preview.wireframe()},
            {"chrome", g_rpcChromeOff ? "off" : "on"},
            {"ortho", ortho},
            {"target", g_cameraTarget.raw},
            {"zoom", g_preview.fitZoom()},
            {"fit", g_preview.fitMode() == PreviewFitMode::Target ? "target" : "all"}};
}

std::string pggViewerApplyRpcRenderArgs(const nlohmann::json& args) {
    if (!args.contains("wire"))
        g_preview.setWireframe(false);
    else
        g_preview.setWireframe(args.value("wire", false));

    if (!args.contains("ortho")) {
        g_preview.setProjection(PreviewProjection::Perspective);
    } else {
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
            return "ortho must be front|side|top|off";
        }
    }

    if (!args.contains("highlight"))
        g_previewOpts.highlightGroup.clear();
    else
        g_previewOpts.highlightGroup = args.value("highlight", std::string{});

    if (!args.contains("shading")) {
        g_previewOpts.shading = PreviewShading::Auto;
    } else {
        const std::string v = args.value("shading", std::string{"auto"});
        if (v == "flat")
            g_previewOpts.shading = PreviewShading::Flat;
        else if (v == "smooth")
            g_previewOpts.shading = PreviewShading::Smooth;
        else if (v == "auto")
            g_previewOpts.shading = PreviewShading::Auto;
        else
            return "shading must be auto|flat|smooth";
    }

    if (!args.contains("colors"))
        g_previewOpts.vertexColors = true;
    else
        g_previewOpts.vertexColors = args.value("colors", true);

    if (!args.contains("chrome")) {
        g_rpcChromeOff = false;
    } else {
        const std::string c = args.value("chrome", std::string{});
        if (c == "off")
            g_rpcChromeOff = true;
        else if (c == "on")
            g_rpcChromeOff = false;
        else
            return "chrome must be 'on' or 'off'";
    }

    g_cameraTargetAutoYaw = !args.contains("orbit");
    if (args.contains("orbit")) {
        const nlohmann::json& o = args["orbit"];
        if (o.is_array() && o.size() >= 2) {
            const float yaw = o[0].get<float>();
            const float pitch = o[1].get<float>();
            const float zoom = o.size() >= 3 ? o[2].get<float>() : g_preview.fitZoom();
            g_preview.setOrbit(yaw, pitch, zoom);
        } else {
            return "orbit must be [yaw, pitch] or [yaw, pitch, zoom]";
        }
    }

    const bool hasOrbitZoom =
        args.contains("orbit") && args["orbit"].is_array() && args["orbit"].size() >= 3;
    if (args.contains("zoom")) {
        g_preview.setZoom(args.value("zoom", 1.0f));
    } else if (!args.contains("distance") && !hasOrbitZoom) {
        g_preview.setZoom(1.0f);
    }

    if (!args.contains("target")) {
        g_cameraTarget = CameraTargetSpec{};
        g_cameraTargetHasBBox = false;
        g_preview.setFitMode(PreviewFitMode::All);
    } else {
        const std::string t = args.value("target", std::string{});
        if (t.empty()) {
            g_cameraTarget = CameraTargetSpec{};
            g_cameraTargetHasBBox = false;
            g_preview.setFitMode(PreviewFitMode::All);
        } else {
            g_cameraTarget = parseCameraTargetSpec(t);
            if (g_cameraTarget.kind == CameraTargetSpec::Kind::None)
                return "unparseable target '" + t +
                       "' (want x,y,z | group:<name> | group:<name>@<binding> | binding:<path>)";
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
            return "fit must be 'all' or 'target'";
        }
    }
    return {};
}

// Registers the --serve command handlers (declared in SmokeTest.h so the
// smoke test can drive the same handlers on its own server instance). The
// handlers close over main.cpp's globals; the server class itself is
// stateless about the viewer.

// RPC diff (C2): runs the loaded file to its declared outputs with the
// session params/cache (the probe/export commands' setup) and fingerprints
// every output (nullopt for sdf/compiled fields — no structural hash). Run
// errors report false with the run_errors message body in `why`; the run
// stats globals update either way (status shows the attempt).
bool runOutputsFingerprints(std::vector<std::pair<std::string, std::optional<uint64_t>>>& outFps,
                            double& outMs, std::string& why) {
    pgg::RunParams rp;
    for (const auto& [name, text] : g_paramValues)
        if (!text.empty()) rp.values.push_back({name, parseCliValue(text)});
    rp.importRoots = g_rpcImportRoots;
    rp.cache = g_memoryCache.get();
    rp.profile = true;
    const double t0 = wallNowSec();
    pgg::RunResult r = pgg::runFile(g_filePath, rp);
    outMs = (wallNowSec() - t0) * 1000.0;
    g_lastRunMs = outMs;
    g_lastCacheHits = r.stats.cacheHits;
    g_lastCacheMisses = r.stats.cacheMisses;
    g_lastProfile = r.stats.profile;
    g_lastRunDiags = r.diagnostics;
    if (r.hasErrors()) {
        for (const pgg::Diagnostic& d : r.diagnostics)
            if (!d.isWarning) why += (why.empty() ? "" : "\n") + d.code + " " + d.message;
        return false;
    }
    outFps.clear();
    for (const pgg::RunOutput& o : r.outputs) {
        uint64_t fp = 0;
        std::optional<uint64_t> v;
        if (pgg::fingerprintValue(o.value, fp)) v = fp;
        outFps.push_back({o.name, v});
    }
    return true;
}

nlohmann::json fingerprintJson(const std::optional<uint64_t>& fp) {
    if (!fp) return nullptr;
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(*fp));
    return buf;
}

void registerPggViewerRpcHandlers(ViewerRpcServer& server) {
    using nlohmann::json;

    server.on("ping", [](uint64_t, const json&) -> std::optional<json> {
        return json{{"pong", true}, {"app", "PggViewer"}, {"protocol", 1}};
    });

    server.on("status", [](uint64_t, const json&) -> std::optional<json> {
        // E (agent_tooling_plan §5): the last run's per-binding wall times,
        // top-20 by exclusive ms (name asc on ties) + the total over all rows.
        json prof = json::array();
        double profTotalMs = 0.0;
        for (const pgg::BindingProfile& b : pgg::profileByTime(g_lastProfile)) {
            profTotalMs += b.ms;
            if (prof.size() < 20)
                prof.push_back({{"name", b.name},
                                {"ms", b.ms},
                                {"field_evals", b.fieldEvals},
                                {"cache_hit", b.cacheHit}});
        }
        return json{{"file", g_filePath},
                    {"params", paramsJson()},
                    {"cache",
                     {{"size", g_memoryCache ? g_memoryCache->size() : 0},
                      {"capacity", g_memoryCache ? g_memoryCache->capacity() : 0},
                      {"hits", g_lastCacheHits},
                      {"misses", g_lastCacheMisses}}},
                    {"preview", {{"target", g_previewTarget}, {"has_value", g_previewHasValue}}},
                    {"profile", prof},
                    {"profile_total_ms", profTotalMs},
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
        // F4: a load{source} temp file is never edited externally (the next
        // load{source} replaces it), so it is exempt from mtime tracking.
        g_mainFileFromRpcSource = args.contains("source");
        // RPC diff (C2): an explicit load is a new document context — the old
        // snapshot is dropped; snapshot:true immediately records a fresh one
        // (runs the outputs; a broken file gets no baseline).
        g_diffSnapshot.reset();
        g_baselineFrames.clear();
        const bool wantSnapshot = args.value("snapshot", false);
        // Static check only (closure -> expand -> typecheck): a broken file is
        // answered in milliseconds, no Engine::run.
        std::vector<std::string> checkRoots = roots;
        const std::string dir = std::filesystem::path(path).parent_path().string();
        if (!dir.empty()) checkRoots.push_back(dir);
        json data = staticCheckJson(g_doc, checkRoots, boundParamNames());
        data["path"] = path;
        if (wantSnapshot) {
            bool recorded = false;
            if (!data.value("has_errors", true)) {
                std::vector<std::pair<std::string, std::optional<uint64_t>>> fps;
                double ms = 0.0;
                std::string why;
                if (!runOutputsFingerprints(fps, ms, why))
                    ViewerRpcServer::fail("run_errors", why.empty() ? "run failed" : why);
                g_diffSnapshot = DiffSnapshot{path, fps};
                recorded = true;
            }
            data["snapshot"] = recorded;
        }
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

    server.on("views", [](uint64_t, const json&) -> std::optional<json> {
        json arr = json::array();
        for (const NamedView& v : g_namedViews) arr.push_back(v.fields);
        return json{{"file", g_filePath}, {"views", arr}};
    });

    server.on("render", [](uint64_t clientId, const json& argsIn) -> std::optional<json> {
        if (!g_state.gfxOk || !g_state.imguiOk)
            ViewerRpcServer::fail("no_frame_loop",
                                  "render needs the frame loop and the preview pane (unavailable with "
                                  "--no-ui or in --smoke)");
        if (g_filePath.empty()) ViewerRpcServer::fail("no_file", "no .pgg file loaded");
        if (g_pendingRender.active) ViewerRpcServer::fail("busy", "a previous render is still pending");
        const AutoReloadResult reload = autoReloadIfChanged();
        std::string viewErr;
        const json args = mergeNamedViewArgs(argsIn, viewErr);
        if (!viewErr.empty()) ViewerRpcServer::fail("bad_args", viewErr);
        const std::string node = args.value("node", std::string{});
        if (node.empty()) ViewerRpcServer::fail("bad_args", "render needs 'node' (or a named view that sets it)");

        bool previewOnly = true;
        if (args.contains("frame")) {
            const std::string f = args.value("frame", std::string{});
            if (f == "preview") {
                previewOnly = true;
            } else if (f == "window") {
                previewOnly = false;
            } else {
                ViewerRpcServer::fail("bad_args", "frame must be 'preview' or 'window'");
            }
        }

        bool comparePrev = false;
        bool compareBaseline = false;
        if (args.contains("compare")) {
            const std::string c = args.value("compare", std::string{});
            if (c == "prev") {
                comparePrev = true;
            } else if (c == "baseline") {
                compareBaseline = true;
            } else {
                ViewerRpcServer::fail("bad_args", "compare must be 'prev' or 'baseline'");
            }
        }
        const bool saveBaseline = args.value("save_baseline", false);
        const bool writePng = args.value("png", true);

        const std::string applyErr = pggViewerApplyRpcRenderArgs(args);
        if (!applyErr.empty()) ViewerRpcServer::fail("bad_args", applyErr);
        const bool chromeOff = g_rpcChromeOff;

        ShotCrop crop;
        crop.previewOnly = previewOnly;
        if (args.contains("size")) {
            const json& sz = args["size"];
            if (sz.is_array() && sz.size() >= 2) {
                if (previewOnly) {
                    crop.wantW = std::max(0, static_cast<int>(std::lround(sz[0].get<float>())));
                    crop.wantH = std::max(0, static_cast<int>(std::lround(sz[1].get<float>())));
                } else {
                    g_cliPreviewSize = ImVec2(sz[0].get<float>(), sz[1].get<float>());
                }
            }
        }

        runPreview(node);
        if (!g_previewHasValue)
            ViewerRpcServer::fail("run_failed",
                                  g_lastPreviewError.empty() ? "run failed for '" + node + "'"
                                                             : g_lastPreviewError);
        if (diagsHaveErrors(g_lastRunDiags)) {
            std::string why;
            for (const pgg::Diagnostic& d : g_lastRunDiags) {
                if (d.isWarning) continue;
                why += (why.empty() ? "" : "\n") + d.code + " " + d.message;
            }
            ViewerRpcServer::fail("run_errors", why);
        }
        if (!g_cameraTargetError.empty()) {
            const std::string why = g_cameraTargetError;
            g_cameraTarget = CameraTargetSpec{};
            g_cameraTargetError.clear();
            g_cameraTargetHasBBox = false;
            g_preview.setFitMode(PreviewFitMode::All);
            ViewerRpcServer::fail("target_unresolved", why);
        }

        if (args.contains("distance")) g_preview.setDistance(args.value("distance", 0.0f));

        std::filesystem::path out;
        if (writePng) {
            const std::string outArg = args.value("out", std::string{});
            if (!outArg.empty()) {
                out = outArg;
            } else {
                out = repoRoot() / "tmp" / "pgg_rpc_shots" /
                      ("shot_" + std::to_string(++g_shotCounter) + ".png");
            }
            std::error_code ec;
            if (out.has_parent_path()) std::filesystem::create_directories(out.parent_path(), ec);
        }

        if (chromeOff) g_chromeOffThisFrame = true;
        g_pendingRender.active = true;
        g_pendingRender.clientId = clientId;
        g_pendingRender.outPath = out.string();
        g_pendingRender.node = node;
        g_pendingRender.previewOnly = previewOnly;
        g_pendingRender.chromeOff = chromeOff;
        g_pendingRender.wantW = crop.wantW;
        g_pendingRender.wantH = crop.wantH;
        g_pendingRender.stats = valueStatsJson(g_previewValue, g_lastRunMs);
        g_pendingRender.diagnostics = diagnosticsJson(g_lastRunDiags);
        g_pendingRender.reloaded = reload.reloaded;
        if (reload.reloaded) g_pendingRender.loadDiagnostics = diagnosticsJson(reload.diags);
        g_pendingRender.cacheHits = g_lastCacheHits;
        g_pendingRender.cacheMisses = g_lastCacheMisses;
        g_pendingRender.comparePrev = comparePrev;
        g_pendingRender.compareBaseline = compareBaseline;
        g_pendingRender.saveBaseline = saveBaseline;
        g_pendingRender.writePng = writePng;
        g_pendingRender.renderState = pggViewerRenderStateJson();
        g_pendingRender.frameKey = currentFrameKey(node, previewOnly, chromeOff);
        return std::nullopt;
    });

    // F3: model vs reference image, side by side. Same two-phase pipeline as
    // render (the capture needs a committed frame), but phase 2 keeps the
    // model frame in memory, composes model|divider|reference into one PNG of
    // the model's height and answers silhouette metrics of both halves. The
    // framing is deterministic: the camera target of earlier render calls is
    // dropped, the fit is forced to the whole scene, and the default view is
    // ortho=front.
    server.on("reference", [](uint64_t clientId, const json& args) -> std::optional<json> {
        if (!g_state.gfxOk || !g_state.imguiOk)
            ViewerRpcServer::fail("no_frame_loop",
                                  "reference needs the frame loop and the preview pane (unavailable with "
                                  "--no-ui or in --smoke)");
        if (g_filePath.empty()) ViewerRpcServer::fail("no_file", "no .pgg file loaded");
        const std::string node = args.value("node", std::string{});
        const std::string image = args.value("image", std::string{});
        if (node.empty() || image.empty())
            ViewerRpcServer::fail("bad_args", "reference needs 'node' and 'image'");
        if (g_pendingRender.active) ViewerRpcServer::fail("busy", "a previous render is still pending");
        // F4: same auto-reload contract as render (the run below reads the
        // current file).
        const AutoReloadResult reload = autoReloadIfChanged();

        // The reference image is read up front — a missing/undecodable file
        // fails the command before any run or frame is spent on it.
        std::vector<std::uint8_t> refPixels;
        int refW = 0, refH = 0;
        if (!loadImageRgba(image, refPixels, refW, refH))
            ViewerRpcServer::fail("invalid_input", "cannot read reference image '" + image + "'");

        g_cameraTargetAutoYaw = !args.contains("orbit");
        if (args.contains("orbit")) {
            const json& o = args["orbit"];
            if (o.is_array() && o.size() >= 2) {
                const float yaw = o[0].get<float>();
                const float pitch = o[1].get<float>();
                const float zoom = o.size() >= 3 ? o[2].get<float>() : 1.0f;
                g_preview.setOrbit(yaw, pitch, zoom);
            }
        }
        if (args.contains("zoom")) g_preview.setZoom(args.value("zoom", 1.0f));
        g_cameraTarget = CameraTargetSpec{};
        g_preview.setFitMode(PreviewFitMode::All);
        {
            const std::string o = args.value("ortho", std::string{"front"});
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
        ShotCrop crop;
        crop.previewOnly = true;  // always the preview viewport (known bg)
        if (args.contains("size")) {
            const json& sz = args["size"];
            if (sz.is_array() && sz.size() >= 2) {
                // Same contract as render frame=preview: target crop size in
                // px, clamped to the actual viewport rect.
                crop.wantW = std::max(0, static_cast<int>(std::lround(sz[0].get<float>())));
                crop.wantH = std::max(0, static_cast<int>(std::lround(sz[1].get<float>())));
            }
        }

        runPreview(node);
        if (!g_previewHasValue)
            ViewerRpcServer::fail("run_failed",
                                  g_lastPreviewError.empty() ? "run failed for '" + node + "'"
                                                             : g_lastPreviewError);
        if (diagsHaveErrors(g_lastRunDiags)) {
            std::string why;
            for (const pgg::Diagnostic& d : g_lastRunDiags) {
                if (d.isWarning) continue;
                why += (why.empty() ? "" : "\n") + d.code + " " + d.message;
            }
            ViewerRpcServer::fail("run_errors", why);
        }
        // runPreview refits only when the pull path changed — force the
        // whole-scene fit so repeated reference calls frame identically.
        g_preview.fit();

        const std::filesystem::path out =
            repoRoot() / "tmp" / "pgg_rpc_shots" / ("ref_" + std::to_string(++g_refCounter) + ".png");
        std::error_code ec;
        std::filesystem::create_directories(out.parent_path(), ec);

        g_pendingRender.active = true;
        g_pendingRender.clientId = clientId;
        g_pendingRender.outPath = out.string();
        g_pendingRender.node = node;
        g_pendingRender.previewOnly = true;
        g_pendingRender.chromeOff = false;
        g_pendingRender.wantW = crop.wantW;
        g_pendingRender.wantH = crop.wantH;
        g_pendingRender.stats = valueStatsJson(g_previewValue, g_lastRunMs);
        g_pendingRender.diagnostics = diagnosticsJson(g_lastRunDiags);
        g_pendingRender.reloaded = reload.reloaded;
        if (reload.reloaded) g_pendingRender.loadDiagnostics = diagnosticsJson(reload.diags);
        g_pendingRender.cacheHits = g_lastCacheHits;
        g_pendingRender.cacheMisses = g_lastCacheMisses;
        g_pendingRender.isReference = true;
        g_pendingRender.refImagePath = image;
        g_pendingRender.refPixels = std::move(refPixels);
        g_pendingRender.refW = refW;
        g_pendingRender.refH = refH;
        return std::nullopt;  // deferred reply from frame()
    });

    server.on("probe", [](uint64_t, const json& args) -> std::optional<json> {
        if (g_filePath.empty()) ViewerRpcServer::fail("no_file", "no .pgg file loaded");
        const std::string spec = args.value("spec", std::string{});
        if (spec.empty()) ViewerRpcServer::fail("bad_args", "probe needs 'spec'");
        const AutoReloadResult reload = autoReloadIfChanged();  // F4: reload on on-disk edits first
        pgg::RunParams rp;
        for (const auto& [name, text] : g_paramValues)
            if (!text.empty()) rp.values.push_back({name, parseCliValue(text)});
        rp.importRoots = g_rpcImportRoots;
        rp.cache = g_memoryCache.get();
        rp.profile = true;
        rp.probes = {spec};
        // Synchronous run by design (MVP), same trade-off as the Probe panel.
        const double t0 = wallNowSec();
        pgg::RunResult r = pgg::runFile(g_filePath, rp);
        const double ms = (wallNowSec() - t0) * 1000.0;
        g_lastRunMs = ms;
        g_lastCacheHits = r.stats.cacheHits;
        g_lastCacheMisses = r.stats.cacheMisses;
        g_lastProfile = r.stats.profile;
        json records = json::array();
        for (const pgg::ProbeRecord& pr : r.probes)
            records.push_back(
                {{"origin", pr.origin}, {"path", pr.path}, {"inspector", pr.inspector}, {"text", pr.text}});
        json data = json{{"records", records},
                         {"diagnostics", diagnosticsJson(r.diagnostics)},
                         {"has_errors", r.hasErrors()},
                         {"ms", ms},
                         {"cache", {{"hits", g_lastCacheHits}, {"misses", g_lastCacheMisses}}},
                         {"reloaded", reload.reloaded}};
        if (reload.reloaded) data["load_diagnostics"] = diagnosticsJson(reload.diags);
        return data;
    });

    server.on("export", [](uint64_t, const json& args) -> std::optional<json> {
        if (g_filePath.empty()) ViewerRpcServer::fail("no_file", "no .pgg file loaded");
        const std::string node = args.value("node", std::string{});
        const std::string objPath = args.value("obj_path", std::string{});
        if (node.empty() || objPath.empty())
            ViewerRpcServer::fail("bad_args", "export needs 'node' and 'obj_path'");
        const AutoReloadResult reload = autoReloadIfChanged();  // F4: reload on on-disk edits first
        pgg::RunParams rp;
        for (const auto& [name, text] : g_paramValues)
            if (!text.empty()) rp.values.push_back({name, parseCliValue(text)});
        rp.importRoots = g_rpcImportRoots;
        rp.cache = g_memoryCache.get();
        rp.profile = true;
        rp.pulls = {node};
        const double t0 = wallNowSec();
        pgg::RunResult r = pgg::runFile(g_filePath, rp);
        const double ms = (wallNowSec() - t0) * 1000.0;
        g_lastRunMs = ms;
        g_lastCacheHits = r.stats.cacheHits;
        g_lastCacheMisses = r.stats.cacheMisses;
        g_lastProfile = r.stats.profile;
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
        json data = json{{"path", objPath},
                         {"realized", realized},
                         {"stats", valueStatsJson(value, ms)},
                         {"cache", {{"hits", g_lastCacheHits}, {"misses", g_lastCacheMisses}}},
                         {"reloaded", reload.reloaded}};
        if (reload.reloaded) data["load_diagnostics"] = diagnosticsJson(reload.diags);
        return data;
    });

    // RPC diff (agent_tooling_plan C2, server half): compares the outputs'
    // structural fingerprints of the CURRENT file against the session
    // snapshot. First call (or a call after an explicit load of another
    // context) records the baseline and answers baseline_created:true; later
    // calls answer per-output identical/changed/added/removed/skipped
    // (sdf/field — no structural fingerprint, fp-level only, no ΔP). The
    // snapshot updates only on diff{update:true} or an explicit load; the F4
    // auto-reload runs INSIDE diff (so a plain on-disk edit is what gets
    // reported) and never clears the snapshot by itself.
    server.on("diff", [](uint64_t, const json& args) -> std::optional<json> {
        if (g_filePath.empty()) ViewerRpcServer::fail("no_file", "no .pgg file loaded");
        const bool update = args.value("update", false);
        const AutoReloadResult reload = autoReloadIfChanged();  // F4: catch the edit first
        std::vector<std::pair<std::string, std::optional<uint64_t>>> fps;
        double ms = 0.0;
        std::string why;
        if (!runOutputsFingerprints(fps, ms, why))
            ViewerRpcServer::fail("run_errors", why.empty() ? "run failed" : why);
        json data;
        data["ms"] = ms;
        data["cache"] = {{"hits", g_lastCacheHits}, {"misses", g_lastCacheMisses}};
        data["reloaded"] = reload.reloaded;
        if (reload.reloaded) data["load_diagnostics"] = diagnosticsJson(reload.diags);
        if (!g_diffSnapshot || g_diffSnapshot->filePath != g_filePath) {
            g_diffSnapshot = DiffSnapshot{g_filePath, fps};
            json outs = json::array();
            for (const auto& [n, fp] : fps)
                outs.push_back({{"name", n}, {"fingerprint", fingerprintJson(fp)}});
            data["baseline_created"] = true;
            data["outputs"] = std::move(outs);
            return data;
        }
        std::map<std::string, std::optional<uint64_t>> prev;
        for (const auto& [n, fp] : g_diffSnapshot->fps) prev[n] = fp;
        json outs = json::array();
        std::map<std::string, bool> seen;
        bool identical = true;
        for (const auto& [n, fp] : fps) {
            seen[n] = true;
            json jo;
            jo["name"] = n;
            jo["fingerprint_now"] = fingerprintJson(fp);
            const auto it = prev.find(n);
            if (it == prev.end()) {
                jo["status"] = "added";
                jo["fingerprint_prev"] = nullptr;
                identical = false;
            } else if (!fp || !it->second) {
                // sdf/field payloads have no structural fingerprint (skipped,
                // does not affect the verdict — same rule as PggTool diff).
                jo["status"] = "skipped";
                jo["note"] = "no structural fingerprint";
                jo["fingerprint_prev"] = fingerprintJson(it->second);
            } else if (*fp == *it->second) {
                jo["status"] = "identical";
                jo["fingerprint_prev"] = fingerprintJson(it->second);
            } else {
                jo["status"] = "changed";
                jo["fingerprint_prev"] = fingerprintJson(it->second);
                identical = false;
            }
            outs.push_back(std::move(jo));
        }
        for (const auto& [n, fp] : g_diffSnapshot->fps) {
            if (seen.count(n)) continue;
            identical = false;
            outs.push_back({{"name", n},
                            {"status", "removed"},
                            {"fingerprint_prev", fingerprintJson(fp)},
                            {"fingerprint_now", nullptr}});
        }
        data["baseline_created"] = false;
        data["identical"] = identical;
        data["outputs"] = std::move(outs);
        data["snapshot_updated"] = update;
        if (update) g_diffSnapshot->fps = fps;
        return data;
    });

    server.on("docs", [](uint64_t, const json& args) -> std::optional<json> {
        const std::string symbol = args.value("symbol", std::string{});
        if (symbol.empty()) ViewerRpcServer::fail("bad_args", "docs needs 'symbol'");
        auto builtinCard = [&](const std::string& name, const std::string& shown) -> json {
            const pgg::BuiltinDoc* bdoc = pgg::findBuiltinDoc(name);
            const pgg::BuiltinSig* bsig = pgg::findBuiltin(name);
            if (!bdoc || !bsig) return {};
            return json{{"symbol", shown},
                        {"kind", "builtin"},
                        {"name", name},
                        {"signature", pgg::builtinSignatureText(*bsig)},
                        {"group", bdoc->group},
                        {"summary", bdoc->summary},
                        {"example", bdoc->example}};
        };
        auto notFound = [&](const std::string& name, const std::string& msg) {
            std::string full = msg;
            const auto near = pgg::suggestBuiltinNames(name);
            if (!near.empty()) {
                full += "; did you mean:";
                for (const std::string& n : near) full += " " + n;
            }
            full += " (try builtin:<name> or PggTool docs builtins | rg -i …)";
            ViewerRpcServer::fail("not_found", full);
        };
        if (symbol.rfind("builtin:", 0) == 0) {
            const std::string name = symbol.substr(8);
            json card = builtinCard(name, symbol);
            if (card.empty()) notFound(name, "builtin not found: " + name);
            return card;
        }
        if (g_doc.file) {
            pgg::DocsLookupResult res = pgg::findDef(*g_doc.file, g_filePath, symbol, g_rpcImportRoots);
            if (res.found)
                return json{{"symbol", symbol},
                            {"kind", "def"},
                            {"signature", res.signature},
                            {"docstring", res.hasDoc ? res.docstring : std::string{}}};
        }
        json card = builtinCard(symbol, symbol);
        if (!card.empty()) return card;
        std::string msg = "def not found: " + symbol;
        if (!g_doc.file) msg = "no .pgg file loaded and not a builtin: " + symbol;
        notFound(symbol, msg);
        return {};
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
        } else if (arg.rfind("--shot-frame=", 0) == 0) {
            const std::string v = arg.substr(13);
            if (v == "preview") {
                g_shotFramePreview = true;
            } else if (v == "window") {
                g_shotFramePreview = false;
            } else {
                spdlog::warn("PggViewer: unknown --shot-frame='{}' (want window|preview)", v);
            }
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
