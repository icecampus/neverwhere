// StoneCubePlayground — incubation playground for the "cube made of stones"
// generator (principles from iq's "Voronoi - rocks", see
// docs/reference/shadertoy). SDF + raymarch now; the idea develops here:
// C++ SDF twin -> surface-nets mesh (highground_core) -> TileShapePlayground
// -> editor tool.
//
// Single-source GLSL (#version 330) with the GLCORE backend on every desktop
// platform (same exception as ShadertoyPlayground, see AGENTS.md).
//
// CLI: --smoke  --shot <file.png>  --scale <0.1..1.0>  --seed <n>

#include "pch.h"

// Project headers first: they pull sokol_gfx.h for plain declarations, so
// they must come before SOKOL_IMPL is defined (this sokol version compiles
// the impl outside the include guard).
#include "StoneCubeScene.h"
#include "StoneMeshView.h"

#include <stone_gen/stone_bake.h>
#include <stone_gen/stone_mesh.h>

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

// stb_image_write implementation lives in stone_gen (stone_bake.cpp); here
// only the declarations are needed (screenshot writer).
#include <stb_image_write.h>

namespace {

struct CliOptions {
    bool smoke = false;
    float renderScale = 0.5f;
    float seed = 0.0f;
    std::string shotPath;
    std::string bakeDir;       // --bake <dir>: mesh + bake + export, then quit
    bool startInMeshMode = false; // --mesh: start on the baked-mesh view
};

struct AppState {
    CliOptions cli;
    stonecube::Params params;
    stonecube::Camera camera;
    stonecube::StoneCubeScene scene;
    bool gfxOk = false;
    bool imguiOk = false;

    // Mesh view (baked geometry + textures, the "fast render" path).
    int viewMode = 0; // 0 = raymarch, 1 = mesh
    stonecube::StoneMeshView meshView;
    stone_gen::StoneMesh mesh;
    stone_gen::BakedTextures baked;
    bool meshDirty = true;
    uint64_t paramChangeTime = 0;
    int bakeSize = 256;
    stonecube::Params paramsSnapshot = {};

    uint64_t lastTime = 0;
    float dt = 1.0f / 60.0f;
    float dtAccum = 0.0f;
    int dtSamples = 0;
    int frameIndex = 0;

    bool draggingRotate = false;
    bool draggingPan = false;
    float dragStartX = 0.0f;
    float dragStartY = 0.0f;
    stonecube::Camera dragCamStart;

    int logErrors = 0;
    int shotFrame = -1;
    bool smokeCheckOk = false;
};

AppState g_state;

void buildMeshAndBake();
void rebake();
bool exportBake(const std::string& dir);

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
        } else if (arg == "--scale" && i + 1 < argc) {
            cli.renderScale = std::clamp(std::stof(argv[++i]), 0.1f, 1.0f);
        } else if (arg == "--seed" && i + 1 < argc) {
            cli.seed = std::stof(argv[++i]);
        } else if (arg == "--shot" && i + 1 < argc) {
            cli.shotPath = argv[++i];
        } else if (arg == "--bake" && i + 1 < argc) {
            cli.bakeDir = argv[++i];
        } else if (arg == "--mesh") {
            cli.startInMeshMode = true;
        }
    }
    return cli;
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
    spdlog::info("StoneCubePlayground: sg_setup() {}", g_state.gfxOk ? "OK" : "FAILED");
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

    g_state.params.shape2[3] = g_state.cli.seed;
    g_state.scene.init();
    g_state.meshView.init();
    g_state.paramsSnapshot = g_state.params;

    if (!g_state.cli.shotPath.empty()) {
        g_state.shotFrame = 60;
    }
    if (!g_state.cli.bakeDir.empty()) {
        g_state.bakeSize = 512;
    }
    if (g_state.cli.startInMeshMode) {
        g_state.viewMode = 1;
    }
}

void drawUi() {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 480.0f), ImGuiCond_Once);
    ImGui::Begin("StoneCubePlayground");
    ImGui::Text("dt: %.2f ms  frame: %d", 1000.0f * g_state.dt, g_state.frameIndex);
    ImGui::Text("fb: %dx%d  dpi: %.2f", sapp_width(), sapp_height(), sapp_dpi_scale());
    ImGui::SliderFloat("Render scale", &g_state.cli.renderScale, 0.25f, 1.0f, "%.2f");
    ImGui::Text("render: %dx%d", g_state.scene.targetWidth(), g_state.scene.targetHeight());
    ImGui::Separator();

    int mode = g_state.viewMode;
    ImGui::RadioButton("Raymarch", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Mesh", &mode, 1);
    if (mode != g_state.viewMode) {
        g_state.viewMode = mode;
        if (mode == 1 && (g_state.mesh.vertices.empty() || g_state.meshDirty)) {
            buildMeshAndBake();
        }
    }
    if (g_state.viewMode == 1) {
        ImGui::Text("mesh: %zu verts, %zu tris", g_state.mesh.vertices.size(),
            g_state.mesh.indices.size() / 3);
        ImGui::Text("watertight bad: %d  saddles: %d", g_state.mesh.watertightBadEdges,
            g_state.mesh.remainingSaddles);
        ImGui::Text("mesh %.0f ms, bake %.0f ms", g_state.mesh.meshMs, g_state.baked.bakeMs);
        if (ImGui::Button("Rebuild")) {
            buildMeshAndBake();
        }
        ImGui::SameLine();
        static const int kSizes[] = {256, 512, 1024};
        int sizeIndex = g_state.bakeSize == 1024 ? 2 : (g_state.bakeSize == 512 ? 1 : 0);
        ImGui::SetNextItemWidth(110.0f);
        if (ImGui::Combo("bake size", &sizeIndex, "256\0512\01024\0")) {
            g_state.bakeSize = kSizes[sizeIndex];
            if (!g_state.mesh.vertices.empty()) {
                rebake();
            }
        }
        if (ImGui::Button("Export")) {
            exportBake("bake_out");
        }
    }
    ImGui::Separator();

    stonecube::Params& p = g_state.params;
    if (ImGui::CollapsingHeader("Shape", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat3("box size", p.boxSize, 0.3f, 2.0f);
        ImGui::SliderFloat("bevel", &p.boxSize[3], 0.0f, 0.3f);
        ImGui::SliderFloat("voronoi scale", &p.shape1[0], 0.5f, 6.0f);
        ImGui::SliderFloat("cell jitter", &p.shape1[1], 0.0f, 1.0f);
        ImGui::SliderFloat("groove depth", &p.shape1[2], 0.0f, 0.6f);
        ImGui::SliderFloat("groove sharpness", &p.shape1[3], 1.0f, 10.0f);
    }
    if (ImGui::CollapsingHeader("Detail", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("fbm amplitude", &p.shape2[0], 0.0f, 0.2f);
        ImGui::SliderFloat("fbm frequency", &p.shape2[1], 1.0f, 16.0f);
        ImGui::SliderFloat("bump strength", &p.shape2[2], 0.0f, 1.0f);
    }
    if (ImGui::CollapsingHeader("Look", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("light yaw", &p.look[0], 0.0f, 6.283f);
        ImGui::SliderFloat("light pitch", &p.look[1], 0.1f, 1.5f);
        ImGui::SliderFloat("moss", &p.look[2], 0.0f, 1.0f);
        ImGui::SliderFloat("tone seed", &p.look[3], 0.0f, 100.0f);
    }
    if (ImGui::Button("New seed")) {
        p.shape2[3] += 1.37f;
        p.look[3] += 7.13f;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset camera")) {
        g_state.camera = stonecube::Camera{};
    }
    ImGui::End();
}

// Builds the stone mesh and (re)bakes the textures at the current bakeSize.
// Synchronous: freezes the UI for a moment in Debug, acceptable for a bake.
void buildMeshAndBake() {
    const stone_gen::StoneSdf sdf(g_state.params);
    const stone_gen::MeshParams meshParams;
    g_state.mesh = stone_gen::generateMesh(sdf, meshParams);
    g_state.meshView.setMesh(g_state.mesh);

    stone_gen::BakeParams bakeParams;
    bakeParams.textureSize = g_state.bakeSize;
    bakeParams.aoTaps = 6;
    g_state.baked = stone_gen::bakeTextures(sdf, g_state.mesh, bakeParams);
    g_state.meshView.setTextures(g_state.baked.size, g_state.baked.albedo,
        g_state.baked.normal);
    g_state.meshDirty = false;
    g_state.paramsSnapshot = g_state.params;
    spdlog::info("mesh: {} verts / {} tris, watertight bad={}, saddles={}, mesh {:.0f} ms, bake {:.0f} ms ({}x{})",
        g_state.mesh.vertices.size(), g_state.mesh.indices.size() / 3,
        g_state.mesh.watertightBadEdges, g_state.mesh.remainingSaddles,
        g_state.mesh.meshMs, g_state.baked.bakeMs, g_state.baked.size, g_state.baked.size);
}

// Re-bakes the textures on the existing mesh (bake-size change).
void rebake() {
    const stone_gen::StoneSdf sdf(g_state.params);
    stone_gen::BakeParams bakeParams;
    bakeParams.textureSize = g_state.bakeSize;
    bakeParams.aoTaps = 6;
    g_state.baked = stone_gen::bakeTextures(sdf, g_state.mesh, bakeParams);
    g_state.meshView.setTextures(g_state.baked.size, g_state.baked.albedo,
        g_state.baked.normal);
    spdlog::info("rebake: {:.0f} ms ({}x{})", g_state.baked.bakeMs,
        g_state.baked.size, g_state.baked.size);
}

bool exportBake(const std::string& dir) {
    if (g_state.mesh.vertices.empty() || g_state.baked.size == 0) {
        spdlog::error("export: nothing to export (build the mesh first)");
        return false;
    }
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::create_directories(dir, ec);
    const fs::path root(dir);
    const bool ok = stone_gen::writeObj((root / "mesh.obj").string(), g_state.mesh) &&
        stone_gen::writePng((root / "albedo.png").string(), g_state.baked.size,
            g_state.baked.size, g_state.baked.albedo) &&
        stone_gen::writePng((root / "normal.png").string(), g_state.baked.size,
            g_state.baked.size, g_state.baked.normal);
    if (ok) {
        spdlog::info("export: mesh.obj + albedo.png + normal.png written to {}", dir);
    } else {
        spdlog::error("export: failed to write into {}", dir);
    }
    return ok;
}

void writeScreenshot(const std::string& path) {
    const int w = sapp_width();
    const int h = sapp_height();
    std::vector<std::uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    stbi_flip_vertically_on_write(1);
    if (stbi_write_png(path.c_str(), w, h, 4, pixels.data(), w * 4)) {
        spdlog::info("StoneCubePlayground: screenshot written to {}", path);
    } else {
        spdlog::error("StoneCubePlayground: screenshot failed: {}", path);
    }
}

void frame() {
    const uint64_t now = stm_now();
    g_state.dt = static_cast<float>(stm_sec(stm_diff(now, g_state.lastTime)));
    g_state.lastTime = now;
    ++g_state.frameIndex;

    g_state.dtAccum += g_state.dt;
    if (++g_state.dtSamples >= 60) {
        spdlog::info("avg dt: {:.2f} ms ({}x{}, scale {:.2f})",
            1000.0f * g_state.dtAccum / g_state.dtSamples,
            g_state.scene.targetWidth(), g_state.scene.targetHeight(),
            g_state.cli.renderScale);
        g_state.dtAccum = 0.0f;
        g_state.dtSamples = 0;
    }

    if (!g_state.gfxOk) {
        return;
    }

    // One-shot actions (--smoke / --bake) run on the first frame.
    if (g_state.frameIndex == 1 && (g_state.cli.smoke || !g_state.cli.bakeDir.empty())) {
        buildMeshAndBake();
        bool ok = !g_state.mesh.vertices.empty() &&
            g_state.mesh.watertightBadEdges == 0 &&
            g_state.mesh.remainingSaddles == 0 &&
            g_state.baked.size > 0;
        if (!g_state.cli.bakeDir.empty()) {
            ok = exportBake(g_state.cli.bakeDir) && ok;
            spdlog::info(ok ? "TEST PASS: bake export finished OK" : "TEST FAIL: bake export failed");
            if (!ok) {
                ++g_state.logErrors;
            }
            sapp_quit();
            return;
        }
        g_state.smokeCheckOk = ok;
    }

    // Debounce: params changed -> rebuild mesh+bake after 0.3 s of quiet.
    if (g_state.viewMode == 1) {
        if (!g_state.meshDirty &&
            std::memcmp(&g_state.params, &g_state.paramsSnapshot,
                sizeof(stonecube::Params)) != 0) {
            g_state.meshDirty = true;
            g_state.paramChangeTime = stm_now();
        }
        if (g_state.meshDirty &&
            stm_sec(stm_diff(stm_now(), g_state.paramChangeTime)) > 0.3) {
            buildMeshAndBake();
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

    if (g_state.viewMode == 0) {
        g_state.scene.drawScene(g_state.params, g_state.camera,
            g_state.cli.renderScale, sapp_width(), sapp_height());
    }

    float lightDir[3];
    {
        const float yaw = g_state.params.look[0];
        const float pitch = g_state.params.look[1];
        lightDir[0] = std::cos(pitch) * std::sin(yaw);
        lightDir[1] = std::sin(pitch);
        lightDir[2] = std::cos(pitch) * std::cos(yaw);
    }

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.07f, 0.08f, 0.10f, 1.0f};
    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);
    if (g_state.viewMode == 0) {
        g_state.scene.drawBlit();
    } else {
        g_state.meshView.draw(g_state.camera, lightDir, sapp_width(), sapp_height());
    }
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
    g_state.scene.shutdown();
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

    stonecube::Camera& cam = g_state.camera;
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
            cam.pitch = std::clamp(g_state.dragCamStart.pitch + dy * 0.008f, -1.5f, 1.5f);
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
        cam.dist = std::clamp(cam.dist * (ev->scroll_y > 0.0f ? 0.9f : 1.1f), 1.2f, 20.0f);
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
    desc.window_title = "StoneCubePlayground";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = logCapture;

    sapp_run(&desc);
    return g_state.cli.smoke && g_state.logErrors > 0 ? 1 : 0;
}
