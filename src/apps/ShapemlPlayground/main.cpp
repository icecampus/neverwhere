// ShapemlPlayground — explorer for the ShapeML grammar-based procedural
// modeling library (https://github.com/stefalie/shapeml, GPL3v3, wired in via
// the shapeml_ref wrapper lib). Loads .shp grammars, derives them on the
// ShapeML interpreter and renders the result with a minimal sokol mesh view:
// grammar list, live parameter controls, seed, orbit camera, OBJ export.
//
// Single-source GLSL (#version 330) with the GLCORE backend on every desktop
// platform (same exception as StoneCube, see AGENTS.md).
//
// CLI: --smoke  --shot <file.png>  --seed <n>  --dir <grammars dir>
//      --grammar <name.shp|path>
//      --export <dir> [--export-name <stem>] [--param name=value]...  — batch
//      OBJ+MTL export: derives the grammar once with the given overrides and
//      writes <dir>/<stem>.obj (the exporter appends .obj/.mtl itself), then
//      quits.

#include "pch.h"

// Project headers first: they pull sokol_gfx.h for plain declarations, so
// they must come before SOKOL_IMPL is defined (this sokol version compiles
// the impl outside the include guard).
#include "GrammarHost.h"
#include "MeshView.h"

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE
    #endif
#endif

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <sokol_app.h>
#include <sokol_gfx.h>
#include <sokol_glue.h>
#include <sokol_log.h>
#include <sokol_time.h>
#include <util/sokol_imgui.h>

#include <stb_image_write.h>

namespace {

struct CliOptions {
    bool smoke = false;
    unsigned seed = 666;
    std::string shotPath;
    std::string grammarDir;   // default: <repo>/external/shapeml/grammars
    std::string grammarName;  // file name inside dir, or a full path
    std::string exportDir;    // --export: batch OBJ export target dir
    std::string exportName;   // --export-name: output stem (default: grammar stem)
    std::vector<std::pair<std::string, std::string>> paramOverrides; // --param name=value
};

struct AppState {
    CliOptions cli;
    std::string repoRoot;
    std::string grammarDir;
    std::vector<std::string> grammarFiles; // display names (file name only)
    std::vector<std::string> grammarPaths; // full paths, same order
    int selectedGrammar = -1;

    shapemlhost::GrammarHost host;
    std::vector<shapemlhost::Param> params;
    unsigned seed = 666;
    bool autoDerive = true;
    bool deriveDirty = false;
    uint64_t paramChangeTime = 0;
    std::string lastError;

    shapemlhost::Camera camera;
    shapemlhost::MeshView meshView;
    shapemlhost::DerivedModel model;

    bool gfxOk = false;
    bool imguiOk = false;
    uint64_t lastTime = 0;
    float dt = 1.0f / 60.0f;
    int frameIndex = 0;

    bool draggingRotate = false;
    bool draggingPan = false;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
    shapemlhost::Camera dragCamStart;

    int logErrors = 0;
    int shotFrame = -1;
    bool smokeCheckOk = false;
    bool exportQuit = false; // --export ran in init; quit on the first frame
};

AppState g_state;

void logCapture(const char* tag, uint32_t logLevel, uint32_t logItem,
    const char* message, uint32_t lineNr, const char* filename, void* userData) {
    // sokol log levels: 0=panic, 1=error, 2=warning, 3+=info/debug.
    if (logLevel <= 1) {
        ++g_state.logErrors;
    }
    slog_func(tag, logLevel, logItem, message, lineNr, filename, userData);
}

CliOptions parseCli(int argc, char* argv[]) {
    CliOptions cli;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke") {
            cli.smoke = true;
        } else if (arg == "--seed" && i + 1 < argc) {
            cli.seed = static_cast<unsigned>(std::stoul(argv[++i]));
        } else if (arg == "--shot" && i + 1 < argc) {
            cli.shotPath = argv[++i];
        } else if (arg == "--dir" && i + 1 < argc) {
            cli.grammarDir = argv[++i];
        } else if (arg == "--grammar" && i + 1 < argc) {
            cli.grammarName = argv[++i];
        } else if (arg == "--export" && i + 1 < argc) {
            cli.exportDir = argv[++i];
        } else if (arg == "--export-name" && i + 1 < argc) {
            cli.exportName = argv[++i];
        } else if (arg == "--param" && i + 1 < argc) {
            const std::string spec = argv[++i];
            const size_t eq = spec.find('=');
            if (eq == std::string::npos || eq == 0) {
                spdlog::error("--param expects name=value, got '{}'", spec);
            } else {
                cli.paramOverrides.emplace_back(spec.substr(0, eq), spec.substr(eq + 1));
            }
        }
    }
    return cli;
}

// Walks up from cwd looking for the repo root marker (same idea as
// baseDataPath in core_context, but Qt-free).
std::string findRepoRoot() {
    std::error_code ec;
    std::filesystem::path dir = std::filesystem::current_path(ec);
    if (ec) {
        return ".";
    }
    for (int i = 0; i < 12; ++i) {
        if (std::filesystem::exists(dir / ".git", ec)) {
            return dir.string();
        }
        if (!dir.has_parent_path() || dir == dir.parent_path()) {
            break;
        }
        dir = dir.parent_path();
    }
    return ".";
}

void fitCameraToModel() {
    const shapemlhost::DerivedModel& model = g_state.model;
    float center[3];
    float diag = 0.0f;
    for (int c = 0; c < 3; ++c) {
        center[c] = 0.5f * (model.aabbMin[c] + model.aabbMax[c]);
        const float e = model.aabbMax[c] - model.aabbMin[c];
        diag += e * e;
    }
    diag = std::sqrt(diag);
    shapemlhost::Camera cam;
    cam.target[0] = center[0];
    cam.target[1] = center[1];
    cam.target[2] = center[2];
    cam.dist = std::clamp(1.2f * std::max(diag, 0.5f), 0.5f, 800.0f);
    g_state.camera = cam;
}

bool deriveAndUpload() {
    g_state.host.setParams(g_state.params);
    shapemlhost::DerivedModel model;
    std::string error;
    if (!g_state.host.derive(g_state.seed, &model, &error)) {
        g_state.lastError = error;
        spdlog::error("derive failed: {}", error);
        return false;
    }
    g_state.lastError.clear();
    g_state.model = std::move(model);
    g_state.meshView.setModel(g_state.model);
    spdlog::info("derive: {} leaves, {} verts, {} tris, {} draws, {:.0f} ms",
        g_state.model.leafCount, g_state.model.vertices.size(),
        g_state.model.indices.size() / 3, g_state.model.draws.size(),
        g_state.model.deriveMs);
    return true;
}

bool loadGrammar(int index) {
    if (index < 0 || index >= static_cast<int>(g_state.grammarFiles.size())) {
        return false;
    }
    const std::string& path = g_state.grammarPaths[index];
    std::string error;
    if (!g_state.host.load(path, &error)) {
        g_state.lastError = error;
        spdlog::error("grammar load failed: {}", error);
        return false;
    }
    g_state.selectedGrammar = index;
    g_state.params = g_state.host.params();
    g_state.lastError.clear();
    spdlog::info("grammar loaded: {} ({} params)", g_state.grammarFiles[index],
        g_state.params.size());
    if (deriveAndUpload()) {
        fitCameraToModel();
        return true;
    }
    return false;
}

void exportObj() {
    const std::string stem = g_state.selectedGrammar >= 0
        ? std::filesystem::path(g_state.grammarFiles[g_state.selectedGrammar])
              .stem().string()
        : std::string("model");
    const std::filesystem::path dir =
        std::filesystem::path(g_state.repoRoot) / "tmp" / "shapeml_export";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string file = (dir / (stem + ".obj")).string();
    std::string error;
    if (g_state.host.exportObj(file, &error)) {
        spdlog::info("export: OBJ written to {}", file);
    } else {
        spdlog::error("export failed: {}", error);
        g_state.lastError = error;
    }
}

// --param name=value overrides, applied on top of the grammar defaults.
// Conversion follows the declared param type; an unknown name is an error
// (a typo in a bake script must not silently produce the default piece).
bool applyParamOverrides() {
    bool ok = true;
    for (const auto& [name, value] : g_state.cli.paramOverrides) {
        shapemlhost::Param* hit = nullptr;
        for (shapemlhost::Param& p : g_state.params) {
            if (p.name == name) {
                hit = &p;
                break;
            }
        }
        if (!hit) {
            spdlog::error("export: unknown param '{}'", name);
            ok = false;
            continue;
        }
        try {
            switch (hit->type) {
            case shapemlhost::Param::Type::Boolean:
                hit->b = (value == "true" || value == "1");
                break;
            case shapemlhost::Param::Type::Int:
                hit->i = std::stoi(value);
                break;
            case shapemlhost::Param::Type::Number:
                hit->f = std::stod(value);
                break;
            case shapemlhost::Param::Type::String:
                hit->s = value;
                break;
            }
        } catch (const std::exception&) {
            spdlog::error("export: bad value '{}' for param '{}'", value, name);
            ok = false;
        }
    }
    return ok;
}

// --export one-shot: derive with overrides, write <dir>/<stem>.obj (+.mtl).
// The stem is passed WITHOUT an extension — the ShapeML exporter appends
// .obj/.mtl itself.
bool runExportCli() {
    if (g_state.selectedGrammar < 0) {
        spdlog::error("export: no grammar loaded (check --grammar)");
        return false;
    }
    if (!applyParamOverrides()) {
        return false;
    }
    if (!deriveAndUpload()) {
        return false;
    }
    const std::string stem = !g_state.cli.exportName.empty()
        ? g_state.cli.exportName
        : std::filesystem::path(g_state.grammarFiles[g_state.selectedGrammar]).stem().string();
    const std::filesystem::path dir = g_state.cli.exportDir;
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const std::string file = (dir / stem).string();
    std::string error;
    if (!g_state.host.exportObj(file, &error)) {
        spdlog::error("export failed: {}", error);
        return false;
    }
    spdlog::info("export: OBJ written to {}.obj", file);
    return true;
}

// --export one-shot runner: shared tail of both grammar-pick branches.
void maybeRunExport() {
    if (g_state.cli.exportDir.empty()) {
        return;
    }
    if (runExportCli()) {
        spdlog::info("TEST PASS: export scenario finished OK");
    } else {
        spdlog::error("TEST FAIL: export scenario failed");
        ++g_state.logErrors;
    }
    g_state.exportQuit = true;
}

void drawUi() {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(370.0f, 560.0f), ImGuiCond_Once);
    ImGui::Begin("ShapemlPlayground");
    ImGui::Text("dt: %.2f ms  frame: %d", 1000.0f * g_state.dt, g_state.frameIndex);
    ImGui::Text("fb: %dx%d  dpi: %.2f", sapp_width(), sapp_height(), sapp_dpi_scale());
    ImGui::Separator();

    // Grammar list.
    const char* preview = g_state.selectedGrammar >= 0
        ? g_state.grammarFiles[g_state.selectedGrammar].c_str() : "<none>";
    if (ImGui::BeginCombo("grammar", preview)) {
        for (int i = 0; i < static_cast<int>(g_state.grammarFiles.size()); ++i) {
            const bool selected = i == g_state.selectedGrammar;
            if (ImGui::Selectable(g_state.grammarFiles[i].c_str(), selected)) {
                loadGrammar(i);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Reload grammar")) {
        loadGrammar(g_state.selectedGrammar);
    }
    ImGui::SameLine();
    if (ImGui::Button("Export OBJ")) {
        exportObj();
    }

    ImGui::Checkbox("Auto derive", &g_state.autoDerive);
    ImGui::SameLine();
    if (!g_state.autoDerive && ImGui::Button("Derive")) {
        deriveAndUpload();
    }

    int seed = static_cast<int>(g_state.seed);
    ImGui::SetNextItemWidth(120.0f);
    if (ImGui::InputInt("seed", &seed, 1, 100)) {
        g_state.seed = static_cast<unsigned>(std::max(seed, 0));
        g_state.deriveDirty = true;
        g_state.paramChangeTime = stm_now();
    }
    ImGui::SameLine();
    if (ImGui::Button("Random")) {
        g_state.seed = static_cast<unsigned>(std::rand());
        g_state.deriveDirty = true;
        g_state.paramChangeTime = stm_now();
    }

    if (g_state.model.leafCount > 0) {
        ImGui::Text("leaves: %d  verts: %zu  tris: %zu",
            g_state.model.leafCount, g_state.model.vertices.size(),
            g_state.model.indices.size() / 3);
        ImGui::Text("draws: %zu  derive: %.0f ms", g_state.model.draws.size(),
            g_state.model.deriveMs);
    }
    if (!g_state.lastError.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", g_state.lastError.c_str());
        ImGui::PopStyleColor();
    }
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool changed = false;
        for (shapemlhost::Param& p : g_state.params) {
            ImGui::PushID(p.name.c_str());
            switch (p.type) {
            case shapemlhost::Param::Type::Boolean:
                changed |= ImGui::Checkbox(p.name.c_str(), &p.b);
                break;
            case shapemlhost::Param::Type::Int:
                changed |= ImGui::DragInt(p.name.c_str(), &p.i, 0.2f);
                break;
            case shapemlhost::Param::Type::Number: {
                float f = static_cast<float>(p.f);
                if (ImGui::DragFloat(p.name.c_str(), &f, 0.05f)) {
                    p.f = f;
                    changed = true;
                }
                break;
            }
            case shapemlhost::Param::Type::String: {
                char buf[256];
                std::snprintf(buf, sizeof(buf), "%s", p.s.c_str());
                if (ImGui::InputText(p.name.c_str(), buf, sizeof(buf))) {
                    p.s = buf;
                    changed = true;
                }
                break;
            }
            }
            ImGui::PopID();
        }
        if (changed) {
            g_state.deriveDirty = true;
            g_state.paramChangeTime = stm_now();
        }
    }

    if (ImGui::Button("Reset camera")) {
        fitCameraToModel();
    }
    ImGui::End();
}

void writeScreenshot(const std::string& path) {
    const int w = sapp_width();
    const int h = sapp_height();
    std::vector<std::uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    if (stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4)) {
        spdlog::info("ShapemlPlayground: screenshot written to {}", path);
    } else {
        spdlog::error("ShapemlPlayground: screenshot failed: {}", path);
    }
}

// Smoke: parse + derive every grammar in the dir, keep the first one on
// screen for the render check.
bool runSmokeDerivations() {
    // Stress-test grammars, not functional ones: test_noise_2D derives 65k
    // leaves (~4 min in Debug), test_noise_3D 247k leaves / 5.9M verts (~5 h
    // in Debug). Run them explicitly via --grammar if needed.
    static const char* kSkipList[] = {"test_noise_2D.shp", "test_noise_3D.shp"};

    bool ok = true;
    int passed = 0;
    for (int i = 0; i < static_cast<int>(g_state.grammarFiles.size()); ++i) {
        const std::string& name = g_state.grammarFiles[i];
        bool skip = false;
        for (const char* skipped : kSkipList) {
            if (name == skipped) {
                skip = true;
                break;
            }
        }
        if (skip) {
            spdlog::info("smoke: {} skipped (heavy stress grammar)", name);
            continue;
        }
        const std::string& path = g_state.grammarPaths[i];
        shapemlhost::GrammarHost host;
        std::string error;
        if (!host.load(path, &error)) {
            spdlog::error("smoke: {} load FAILED: {}", name, error);
            ok = false;
            continue;
        }
        shapemlhost::DerivedModel model;
        if (!host.derive(g_state.seed, &model, &error)) {
            spdlog::error("smoke: {} derive FAILED: {}", name, error);
            ok = false;
            continue;
        }
        if (model.vertices.empty() || model.indices.empty()) {
            // Not a failure: some grammars (e.g. test_functions_and_attributes)
            // exercise functions/occlusion and legitimately derive to nothing
            // renderable.
            spdlog::warn("smoke: {} produced empty geometry (skipping render check)", name);
            ++passed;
            continue;
        }
        spdlog::info("smoke: {} OK ({} leaves, {} verts, {} tris, {:.0f} ms)",
            name, model.leafCount, model.vertices.size(),
            model.indices.size() / 3, model.deriveMs);
        ++passed;
    }
    spdlog::info("smoke: {}/{} grammars derived OK", passed,
        g_state.grammarFiles.size());
    return ok && passed > 0;
}

void init() {
    spdlog::set_level(spdlog::level::info);
    stm_setup();
    g_state.lastTime = stm_now();

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = logCapture;
    sg_setup(&desc);
    g_state.gfxOk = sg_isvalid();
    spdlog::info("ShapemlPlayground: sg_setup() {}", g_state.gfxOk ? "OK" : "FAILED");
    if (!g_state.gfxOk) {
        return;
    }

    simgui_desc_t imguiDesc = {};
    imguiDesc.no_default_font = true;
    simgui_setup(&imguiDesc);
    g_state.imguiOk = true;
    {
        ImGuiIO& io = ImGui::GetIO();
        const char* fontCandidates[] = {
            "/System/Library/Fonts/Helvetica.ttc",
            "C:/Windows/Fonts/arial.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        };
        bool fontLoaded = false;
        for (const char* path : fontCandidates) {
            std::error_code ec;
            if (std::filesystem::is_regular_file(path, ec)) {
                io.Fonts->AddFontFromFileTTF(path, 17.0f, nullptr,
                    io.Fonts->GetGlyphRangesCyrillic());
                fontLoaded = true;
                break;
            }
        }
        if (!fontLoaded) {
            io.Fonts->AddFontDefault();
        }
    }

    g_state.repoRoot = findRepoRoot();
    g_state.grammarDir = !g_state.cli.grammarDir.empty()
        ? g_state.cli.grammarDir
        : (std::filesystem::path(g_state.repoRoot) / "external" / "shapeml" /
              "grammars").string();
    // Own grammars first (playground folder), then the ShapeML stock ones.
    // Entries keep the full path so both dirs can be listed together.
    const std::string ownDir = (std::filesystem::path(g_state.repoRoot) /
        "src" / "apps" / "ShapemlPlayground" / "grammars").string();
    g_state.grammarFiles = shapemlhost::GrammarHost::scanGrammars(ownDir);
    for (const std::string& name : g_state.grammarFiles) {
        g_state.grammarPaths.push_back(
            (std::filesystem::path(ownDir) / name).string());
    }
    for (const std::string& name :
        shapemlhost::GrammarHost::scanGrammars(g_state.grammarDir)) {
        g_state.grammarFiles.push_back(name);
        g_state.grammarPaths.push_back(
            (std::filesystem::path(g_state.grammarDir) / name).string());
    }
    spdlog::info("ShapemlPlayground: {} grammars (own: {}, stock dir: {})",
        g_state.grammarFiles.size(), ownDir, g_state.grammarDir);

    g_state.seed = g_state.cli.seed;
    g_state.meshView.init();

    if (!g_state.cli.shotPath.empty()) {
        g_state.shotFrame = 60;
    }

    if (g_state.cli.smoke) {
        // Derivations run on the first frame (gfx is up, no pass open yet).
        return;
    }

    // Pick the grammar: --grammar by name/path, otherwise the first one.
    int initial = 0;
    if (!g_state.cli.grammarName.empty()) {
        initial = -1;
        for (int i = 0; i < static_cast<int>(g_state.grammarFiles.size()); ++i) {
            if (g_state.grammarFiles[i] == g_state.cli.grammarName) {
                initial = i;
                break;
            }
        }
        if (initial < 0 &&
            std::filesystem::path(g_state.cli.grammarName).is_absolute()) {
            // Full path outside the scanned dir.
            std::string error;
            if (g_state.host.load(g_state.cli.grammarName, &error)) {
                g_state.params = g_state.host.params();
                if (deriveAndUpload()) {
                    fitCameraToModel();
                }
            } else {
                g_state.lastError = error;
                spdlog::error("grammar load failed: {}", error);
            }
            maybeRunExport();
            return;
        }
        if (initial < 0) {
            spdlog::warn("grammar '{}' not found in {}, falling back",
                g_state.cli.grammarName, g_state.grammarDir);
            initial = 0;
        }
    }
    if (!g_state.grammarFiles.empty()) {
        loadGrammar(initial);
    }
    maybeRunExport();
}

void frame() {
    const uint64_t now = stm_now();
    g_state.dt = static_cast<float>(stm_sec(stm_diff(now, g_state.lastTime)));
    g_state.lastTime = now;
    ++g_state.frameIndex;

    if (!g_state.gfxOk) {
        if (g_state.cli.smoke || !g_state.cli.shotPath.empty() ||
            !g_state.cli.exportDir.empty()) {
            spdlog::error("TEST FAIL: sg_setup failed, no graphics device");
            ++g_state.logErrors;
            sapp_quit();
        }
        return;
    }

    if (g_state.exportQuit) {
        sapp_quit();
        return;
    }

    if (g_state.cli.smoke && g_state.frameIndex == 1) {
        g_state.smokeCheckOk = runSmokeDerivations();
        // Leave the first grammar on screen for the render leg.
        if (!g_state.grammarFiles.empty()) {
            loadGrammar(0);
        }
    }

    // Debounce: params/seed changed -> re-derive after 0.3 s of quiet.
    if (!g_state.cli.smoke && g_state.deriveDirty && g_state.autoDerive &&
        g_state.selectedGrammar >= 0) {
        if (stm_sec(stm_diff(stm_now(), g_state.paramChangeTime)) > 0.3) {
            g_state.deriveDirty = false;
            deriveAndUpload();
        }
    }

    if (g_state.imguiOk) {
        simgui_frame_desc_t fd = {};
        fd.width = sapp_width();
        fd.height = sapp_height();
        fd.delta_time = g_state.dt;
        fd.dpi_scale = sapp_dpi_scale();
        simgui_new_frame(&fd);
        if (!g_state.cli.smoke) {
            drawUi();
        }
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.07f, 0.08f, 0.10f, 1.0f};
    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    g_state.meshView.draw(g_state.camera, sapp_width(), sapp_height());
    if (g_state.imguiOk) {
        simgui_render();
    }
    sg_end_pass();
    sg_commit();

    if (g_state.shotFrame > 0) {
        --g_state.shotFrame;
        if (g_state.shotFrame == 0) {
            writeScreenshot(g_state.cli.shotPath);
            sapp_quit();
        }
    }
    if (g_state.cli.smoke && g_state.frameIndex >= 60) {
        if (g_state.logErrors == 0 && g_state.smokeCheckOk) {
            spdlog::info("TEST PASS: smoke scenario finished OK");
        } else {
            spdlog::error("TEST FAIL: smoke scenario failed ({} log errors, check {})",
                g_state.logErrors, g_state.smokeCheckOk);
        }
        sapp_quit();
    }
}

void cleanup() {
    g_state.meshView.shutdown();
    if (g_state.imguiOk) {
        simgui_shutdown();
        g_state.imguiOk = false;
    }
    if (sg_isvalid()) {
        sg_shutdown();
    }
}

void event(const sapp_event* ev) {
    if (g_state.imguiOk) {
        simgui_handle_event(ev);
    }
    if (g_state.imguiOk) {
        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantCaptureMouse) {
            return;
        }
    }

    shapemlhost::Camera& cam = g_state.camera;
    switch (ev->type) {
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
            g_state.draggingRotate = true;
        } else if (ev->mouse_button == SAPP_MOUSEBUTTON_RIGHT) {
            g_state.draggingPan = true;
        } else {
            break;
        }
        g_state.dragStartX = ev->mouse_x;
        g_state.dragStartY = ev->mouse_y;
        g_state.dragCamStart = cam;
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        g_state.draggingRotate = false;
        g_state.draggingPan = false;
        break;
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        if (g_state.draggingRotate) {
            const float dx = ev->mouse_x - g_state.dragStartX;
            const float dy = ev->mouse_y - g_state.dragStartY;
            cam.yaw = g_state.dragCamStart.yaw + dx * 0.008f;
            cam.pitch = std::clamp(g_state.dragCamStart.pitch + dy * 0.008f,
                -1.5f, 1.5f);
        } else if (g_state.draggingPan) {
            const float dx = ev->mouse_x - g_state.dragStartX;
            const float dy = ev->mouse_y - g_state.dragStartY;
            const float k = cam.dist * 0.0016f;
            const float cy = std::cos(g_state.dragCamStart.yaw);
            const float sy = std::sin(g_state.dragCamStart.yaw);
            // Pan in the camera plane (right = (cy, 0, -sy), up ~ (0, 1, 0)).
            cam.target[0] = g_state.dragCamStart.target[0] - dx * k * cy;
            cam.target[2] = g_state.dragCamStart.target[2] + dx * k * sy;
            cam.target[1] = g_state.dragCamStart.target[1] + dy * k;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_SCROLL:
        cam.dist = std::clamp(cam.dist * (ev->scroll_y > 0.0f ? 0.9f : 1.1f),
            0.05f, 2000.0f);
        break;
    default:
        break;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    g_state.cli = parseCli(argc, argv);

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1280;
    desc.height = 720;
    desc.sample_count = 1;
    desc.window_title = "ShapemlPlayground";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = logCapture;

    sapp_run(&desc);
    // One-shot modes report failure via the exit code.
    const bool oneShot = g_state.cli.smoke || !g_state.cli.shotPath.empty();
    const bool failed = g_state.logErrors > 0 ||
        (g_state.cli.smoke && !g_state.smokeCheckOk);
    return oneShot && failed ? 1 : 0;
}
