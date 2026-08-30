// PggViewer: read-only node-graph projection of .pgg files (spec §10, stage E8).
//   PggViewer <file.pgg> [--shot=out.png] [--zoom=Z] [--center=X,Y] [--no-ui]
//   PggViewer --smoke
// The graph is derived from the text (no separate storage): names are nodes,
// uses are wires, def calls collapse into diveable nodes addressed by their
// instance path, repeat/foreach zones draw as subgraphs with iteration ports
// and a state loop. Layout hints live in trailing `# @pos X Y` comments and
// are written back on node drags (in memory; Save persists). The probe panel
// reuses the E6 probe API (PggTool --probe).

#include "pch.h"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <pgg/eval.h>
#include <pgg/pgg.h>
#include <pgg/src/eval/modules.h>
#include <pgg/src/graph.h>
#include <pgg/src/layout.h>

#include "GraphCanvas.h"
#include "SmokeTest.h"

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

// --- CLI ----------------------------------------------------------------------------

std::string g_pendingLoad;
std::string g_shotPath;
std::optional<float> g_cliZoom;
std::optional<ImVec2> g_cliCenter;
std::string g_cliDive;
bool g_noUi = false;

constexpr float kPanelWidth = 380.0f;

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
        std::vector<std::string> roots;
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
    rp.probes = {target + ":" + inspector};
    // Synchronous run by design (MVP): heavy graphs block the UI for seconds.
    pgg::RunResult r = pgg::runFile(g_filePath, rp);
    std::string out;
    for (const pgg::ProbeRecord& pr : r.probes) out += pr.origin + " " + pr.path + ": " + pr.text + "\n";
    for (const pgg::Diagnostic& d : r.diagnostics) out += pgg::formatDiagnostic(d, g_filePath) + "\n";
    if (out.empty()) out = "(no records)";
    g_probeText = std::move(out);
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

    // File.
    ImGui::InputText("##path", g_pathBuf, sizeof(g_pathBuf));
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
            }
        }
        if (!g_probeText.empty()) {
            ImGui::BeginChild("##probeout", ImVec2(0.0f, 140.0f), true);
            ImGui::TextWrapped("%s", g_probeText.c_str());
            ImGui::EndChild();
        }
    }

    // Launch params of the file (used by probe runs).
    if (!g_paramValues.empty() && ImGui::CollapsingHeader("Params")) {
        for (auto& [name, text] : g_paramValues) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s", text.c_str());
            if (ImGui::InputText(name.c_str(), buf, sizeof(buf))) text = buf;
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
        ImGui::TextWrapped("LMB drag node: move (writes a # @pos hint on release; Save persists).");
        ImGui::TextWrapped("LMB drag empty / RMB drag: pan. Wheel: zoom to cursor.");
        ImGui::TextWrapped("Double-click a def node: dive into the instance body. Esc / breadcrumb: back.");
        ImGui::TextWrapped("Click a node, then run an inspector in the Probe section (E6 probes).");
        ImGui::TextWrapped("Orange wire: zone state loop. Blue dot: the node has a layout hint.");
    }
    ImGui::End();
}

void drawCanvasWindow(int w, int h) {
    const float x0 = panelWidth();
    ImGui::SetNextWindowPos(ImVec2(x0, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(w) - x0, static_cast<float>(h)), ImGuiCond_Always);
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
    if (scope) {
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
    }
    ImGui::End();
}

// Portable --shot capture (GL readback; the documented sokol/GL trap keeps
// this in the TU that owns SOKOL_IMPL — glad must never join them).
bool capturePng(const char* path) {
#if defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)
    const int width = sapp_width();
    const int height = sapp_height();
    if (!path || path[0] == '\0' || width <= 0 || height <= 0) return false;
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width) * height * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    const int ok = stbi_write_png(path, width, height, 4, pixels.data(), width * 4);
    stbi_flip_vertically_on_write(0);
    if (!ok) {
        spdlog::error("capturePng: stbi_write_png failed for {}", path);
        return false;
    }
    return true;
#else
    spdlog::error("capturePng: --shot is only implemented for the GL backends");
    return false;
#endif
}

void init() {
    spdlog::set_level(spdlog::level::info);
    spdlog::info("PggViewer: init()");

    stm_setup();
    g_state.lastTime = stm_now();

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
        simgui_setup(&imgui_desc);
        g_state.imguiOk = true;
    }

    if (!g_pendingLoad.empty()) loadFile(g_pendingLoad);
    // --dive=<instance path>: open a def body directly (deterministic shots).
    if (!g_cliDive.empty() && g_project.scopeOf(g_cliDive)) {
        g_dive.push_back(g_cliDive);
        g_needFitView = true;
    }
}

void frame() {
    const uint64_t now = stm_now();
    g_state.dt = static_cast<float>(stm_sec(stm_diff(now, g_state.lastTime)));
    g_state.lastTime = now;

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
        drawPanel(w, h);
        drawCanvasWindow(w, h);
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.1f, 0.11f, 0.13f, 1.0f};
    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    if (g_state.imguiOk) simgui_render();
    sg_end_pass();
    sg_commit();

    // Headless capture: the graph is laid out at load time, so a short
    // wall-time settle is enough before grabbing the framebuffer.
    if (!g_shotPath.empty() && stm_sec(stm_now()) >= 1.0) {
        if (capturePng(g_shotPath.c_str())) {
            spdlog::info("PggViewer: screenshot saved to {}", g_shotPath);
        } else {
            spdlog::error("PggViewer: screenshot capture failed ({})", g_shotPath);
        }
        g_shotPath.clear();
        sapp_quit();
    }
}

void cleanup() {
    if (g_state.imguiOk) {
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
        } else if (arg.rfind("--zoom=", 0) == 0) {
            g_cliZoom = static_cast<float>(std::atof(arg.substr(7).c_str()));
        } else if (arg.rfind("--center=", 0) == 0) {
            g_cliCenter = parseVec2Arg(arg.substr(9));
        } else if (arg.rfind("--dive=", 0) == 0) {
            g_cliDive = arg.substr(7);
        } else if (arg.rfind("--", 0) != 0) {
            g_pendingLoad = arg;
        }
    }

    if (smoke) {
        spdlog::set_level(spdlog::level::info);
        return runPggViewerSmokeTest() ? 0 : 1;
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
