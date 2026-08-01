// ShadertoyPlayground — universal host for the shadertoy reference demos in
// docs/reference/shadertoy (see README there). Single-source GLSL (#version
// 330) with the GLCORE backend on every desktop platform — unlike the other
// standalone apps, which use Metal on macOS (see AGENTS.md).
//
// Adding a demo = dropping a folder into docs/reference/shadertoy, no
// recompile needed (see docs/reference/shadertoy/README.md).
//
// CLI: --dir <root>  --demo "name"  --list  --smoke  --shot <file.png>

#include "pch.h"

// Project headers first: they pull sokol_gfx.h for plain declarations, so
// they must come before SOKOL_IMPL is defined (same convention as
// TextureBlendLandscape — this sokol version compiles the impl outside the
// include guard and double-including it with SOKOL_IMPL is an error).
#include "CubePreview.h"
#include "DemoDiscovery.h"
#include "ShadertoyRuntime.h"

#define SOKOL_IMPL
#define SOKOL_NO_ENTRY

// Force the GLCORE backend everywhere unless one was already selected:
// the demos are single-source GLSL, porting them per-backend (HLSL/MSL)
// defeats the purpose of the playground.
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

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace {

struct CliOptions {
    std::string dir;
    std::string demoName;
    bool list = false;
    bool smoke = false;
    bool cube = false;
    float renderScale = 0.5f;
    std::string shotPath;
};

struct MouseState {
    float x = 0.0f;       // framebuffer pixels, bottom-up origin
    float y = 0.0f;
    float clickX = 0.0f;
    float clickY = 0.0f;
    bool down = false;
};

struct AppState {
    CliOptions cli;
    std::vector<shadertoy::Demo> demos;
    int selected = -1;
    shadertoy::ShadertoyRuntime runtime;
    shadertoy::CubePreview cube;
    bool gfxOk = false;
    bool imguiOk = false;

    uint64_t lastTime = 0;
    float dt = 1.0f / 60.0f;
    float dtAccum = 0.0f;
    int dtSamples = 0;
    uint64_t demoStartTime = 0;
    int demoFrame = 0;
    MouseState mouse;

    // smoke mode
    int smokeIndex = 0;
    int smokeFrame = 0;
    int smokeFailures = 0;
    int smokeLogErrors = 0;
    bool smokeVerdictPending = false;

    int shotFrame = -1; // >= 0: capture the framebuffer on this frame count
};

AppState g_state;

// Log tail for the UI + smoke verdicts (sokol warnings/errors land here).
std::string g_logTail;

void logCapture(const char* tag, uint32_t logLevel, uint32_t logItem,
    const char* message, uint32_t lineNr, const char* filename, void* userData) {
    // sokol log levels: 0=panic, 1=error, 2=warning, 3+=info/debug.
    if (logLevel <= 2 && message != nullptr) { // warning and worse
        char buf[1024];
        std::snprintf(buf, sizeof(buf), "[%s] %u: %s\n",
            tag != nullptr ? tag : "?", logItem, message);
        g_logTail += buf;
        if (g_logTail.size() > 8192) {
            g_logTail.erase(0, g_logTail.size() - 8192);
        }
    }
    if (logLevel <= 1) { // panic / error
        ++g_state.smokeLogErrors;
    }
    slog_func(tag, logLevel, logItem, message, lineNr, filename, userData);
}

CliOptions parseCli(int argc, char* argv[]) {
    CliOptions cli;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--smoke") {
            cli.smoke = true;
        } else if (arg == "--list") {
            cli.list = true;
        } else if (arg == "--cube") {
            cli.cube = true;
        } else if (arg == "--scale" && i + 1 < argc) {
            cli.renderScale = std::clamp(std::stof(argv[++i]), 0.1f, 1.0f);
        } else if (arg == "--dir" && i + 1 < argc) {
            cli.dir = argv[++i];
        } else if (arg == "--demo" && i + 1 < argc) {
            cli.demoName = argv[++i];
        } else if (arg == "--shot" && i + 1 < argc) {
            cli.shotPath = argv[++i];
        }
    }
    return cli;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

int findDemoByName(const std::string& name) {
    const std::string needle = toLower(name);
    for (int i = 0; i < static_cast<int>(g_state.demos.size()); ++i) {
        if (toLower(g_state.demos[static_cast<size_t>(i)].name).find(needle) != std::string::npos) {
            return i;
        }
    }
    return -1;
}

void updateDate(float out[4]) {
    const std::time_t now = std::time(nullptr);
    std::tm tm = *std::localtime(&now);
    out[0] = static_cast<float>(tm.tm_year + 1900);
    out[1] = static_cast<float>(tm.tm_mon);
    out[2] = static_cast<float>(tm.tm_mday);
    out[3] = static_cast<float>(tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec);
}

void fillFrameParams(shadertoy::FrameParams& fp) {
    fp.width = static_cast<float>(sapp_width());
    fp.height = static_cast<float>(sapp_height());
    fp.timeSec = static_cast<float>(stm_sec(stm_diff(stm_now(), g_state.demoStartTime)));
    fp.timeDelta = g_state.dt;
    fp.frameIndex = g_state.demoFrame;
    fp.mouse[0] = g_state.mouse.x;
    fp.mouse[1] = g_state.mouse.y;
    fp.mouse[2] = g_state.mouse.down ? g_state.mouse.clickX : -g_state.mouse.clickX;
    fp.mouse[3] = g_state.mouse.down ? g_state.mouse.clickY : -g_state.mouse.clickY;
    updateDate(fp.date);
}

void selectDemo(int index) {
    if (index < 0 || index >= static_cast<int>(g_state.demos.size())) {
        return;
    }
    const shadertoy::Demo& demo = g_state.demos[static_cast<size_t>(index)];
    if (!demo.hasImage) {
        return;
    }
    g_logTail.clear();
    g_state.smokeLogErrors = 0;
    if (g_state.runtime.loadDemo(demo, sapp_width(), sapp_height())) {
        g_state.selected = index;
        g_state.demoStartTime = stm_now();
        g_state.demoFrame = 0;
    } else {
        // In smoke mode the attempt still counts as the current slot, so the
        // verdict prints FAIL and the cycle advances instead of retrying.
        if (g_state.cli.smoke) {
            g_state.selected = index;
        }
        spdlog::error("ShadertoyPlayground: load failed: {}", g_state.runtime.lastError());
    }
}

void init() {
    spdlog::set_level(spdlog::level::info);
    stm_setup();
    g_state.lastTime = stm_now();
    g_state.demoStartTime = g_state.lastTime;

    sg_desc desc = {};
    desc.environment = sglue_environment();
    desc.logger.func = logCapture;
    sg_setup(&desc);
    g_state.gfxOk = sg_isvalid();
    spdlog::info("ShadertoyPlayground: sg_setup() {}", g_state.gfxOk ? "OK" : "FAILED");
    if (!g_state.gfxOk) {
        return;
    }

    simgui_desc_t imguiDesc = {};
    imguiDesc.no_default_font = true; // need Cyrillic for the demo descriptions
    simgui_setup(&imguiDesc);
    g_state.imguiOk = true;
    {
        ImGuiIO& io = ImGui::GetIO();
        const char* fontCandidates[] = {
            "/System/Library/Fonts/Helvetica.ttc",             // macOS
            "C:/Windows/Fonts/arial.ttf",                      // Windows
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", // Linux
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

    g_state.runtime.init();
    g_state.runtime.cubeMode = g_state.cli.cube;
    g_state.runtime.renderScale = g_state.cli.renderScale;
    g_state.cube.init();

    const std::filesystem::path root = shadertoy::resolveShadertoyRoot(g_state.cli.dir);
    if (root.empty()) {
        spdlog::error("ShadertoyPlayground: shadertoy root not found (tried docs/reference/shadertoy upwards from cwd)");
    } else {
        spdlog::info("ShadertoyPlayground: scanning {}", root.string());
        g_state.demos = shadertoy::scanDemos(root);
        spdlog::info("ShadertoyPlayground: {} demos found ({} with Image pass)",
            g_state.demos.size(),
            std::count_if(g_state.demos.begin(), g_state.demos.end(),
                [](const shadertoy::Demo& d) { return d.hasImage; }));
    }

    int initial = -1;
    if (!g_state.cli.demoName.empty()) {
        initial = findDemoByName(g_state.cli.demoName);
        if (initial < 0) {
            spdlog::warn("ShadertoyPlayground: demo '{}' not found", g_state.cli.demoName);
        } else if (!g_state.demos[static_cast<size_t>(initial)].hasImage) {
            spdlog::warn("ShadertoyPlayground: demo '{}' is a stub (no Image pass)",
                g_state.demos[static_cast<size_t>(initial)].name);
            initial = -1;
        }
    }
    if (initial < 0) {
        for (int i = 0; i < static_cast<int>(g_state.demos.size()); ++i) {
            if (g_state.demos[static_cast<size_t>(i)].hasImage) {
                initial = i;
                break;
            }
        }
    }
    if (g_state.cli.smoke) {
        // Smoke starts from the first runnable demo; selectDemo happens per slot.
        g_state.smokeIndex = 0;
        g_state.smokeFrame = 0;
    } else if (initial >= 0) {
        selectDemo(initial);
    }
    if (!g_state.cli.shotPath.empty()) {
        g_state.shotFrame = 60; // capture after warm-up
    }
}

void drawUi() {
    ImGui::SetNextWindowPos(ImVec2(12.0f, 12.0f), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(360.0f, 560.0f), ImGuiCond_Once);
    ImGui::Begin("ShadertoyPlayground");
    ImGui::Text("dt: %.2f ms  frame: %d", 1000.0f * g_state.dt, g_state.demoFrame);
    ImGui::Text("fb: %dx%d  dpi: %.2f", sapp_width(), sapp_height(), sapp_dpi_scale());
    ImGui::Separator();

    ImGui::TextUnformatted("Demos:");
    ImGui::BeginChild("demo_list", ImVec2(0, 220), ImGuiChildFlags_Borders);
    for (int i = 0; i < static_cast<int>(g_state.demos.size()); ++i) {
        const shadertoy::Demo& demo = g_state.demos[static_cast<size_t>(i)];
        if (!demo.hasImage) {
            ImGui::TextDisabled("%s (stub)", demo.name.c_str());
            continue;
        }
        if (ImGui::Selectable(demo.name.c_str(), i == g_state.selected)) {
            selectDemo(i);
        }
    }
    ImGui::EndChild();

    if (g_state.selected >= 0) {
        const shadertoy::Demo& demo = g_state.demos[static_cast<size_t>(g_state.selected)];
        if (!demo.description.empty()) {
            ImGui::TextWrapped("%s", demo.description.c_str());
        }
    }
    ImGui::Separator();

    int mode = g_state.runtime.cubeMode ? 1 : 0;
    ImGui::RadioButton("Fullscreen", &mode, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Cube", &mode, 1);
    g_state.runtime.cubeMode = mode == 1;

    ImGui::SliderFloat("Render scale", &g_state.runtime.renderScale, 0.25f, 1.0f, "%.2f");
    ImGui::Text("render: %dx%d", g_state.runtime.scaledWidth((float)sapp_width()),
        g_state.runtime.scaledHeight((float)sapp_height()));

    if (ImGui::Button("Reload (R)")) {
        if (g_state.runtime.reloadDemo()) {
            g_state.demoStartTime = stm_now();
            g_state.demoFrame = 0;
        }
    }
    if (!g_state.runtime.lastError().empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.35f, 0.35f, 1.0f));
        ImGui::TextWrapped("%s", g_state.runtime.lastError().c_str());
        ImGui::PopStyleColor();
    }
    if (!g_logTail.empty()) {
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.7f, 0.3f, 1.0f));
        ImGui::TextWrapped("%s", g_logTail.c_str());
        ImGui::PopStyleColor();
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
        spdlog::info("ShadertoyPlayground: screenshot written to {}", path);
    } else {
        spdlog::error("ShadertoyPlayground: screenshot failed: {}", path);
    }
}

void smokeVerdictAndAdvance() {
    const bool ok = g_state.selected >= 0 && g_state.runtime.isLoaded() &&
        g_state.smokeLogErrors == 0;
    const char* name = g_state.selected >= 0
        ? g_state.demos[static_cast<size_t>(g_state.selected)].name.c_str()
        : "<none>";
    if (ok) {
        spdlog::info("TEST PASS {}", name);
    } else {
        spdlog::error("TEST FAIL {} (loaded={}, log errors={})",
            name, g_state.runtime.isLoaded(), g_state.smokeLogErrors);
        ++g_state.smokeFailures;
    }
    // Next runnable demo.
    int next = -1;
    for (int i = g_state.smokeIndex + 1; i < static_cast<int>(g_state.demos.size()); ++i) {
        if (g_state.demos[static_cast<size_t>(i)].hasImage) {
            next = i;
            break;
        }
    }
    if (next < 0) {
        spdlog::info(g_state.smokeFailures == 0
            ? "TEST PASS: smoke scenario finished OK"
            : "TEST FAIL: smoke scenario failed");
        sapp_quit();
        return;
    }
    g_state.smokeIndex = next;
    g_state.smokeFrame = 0;
    selectDemo(next);
}

void frame() {
    const uint64_t now = stm_now();
    g_state.dt = static_cast<float>(stm_sec(stm_diff(now, g_state.lastTime)));
    g_state.lastTime = now;

    // Average dt over 60-frame windows: single samples are useless for perf
    // judgements (the demos' camera moves, cost varies by scene region).
    g_state.dtAccum += g_state.dt;
    if (++g_state.dtSamples >= 60) {
        spdlog::info("avg dt: {:.2f} ms ({}x{}, scale {:.2f})",
            1000.0f * g_state.dtAccum / g_state.dtSamples,
            g_state.runtime.scaledWidth((float)sapp_width()),
            g_state.runtime.scaledHeight((float)sapp_height()),
            g_state.runtime.renderScale);
        g_state.dtAccum = 0.0f;
        g_state.dtSamples = 0;
    }

    if (!g_state.gfxOk) {
        return;
    }

    if (g_state.cli.smoke) {
        if (g_state.selected < 0) {
            // Kick off: select the first runnable demo.
            int first = -1;
            for (int i = 0; i < static_cast<int>(g_state.demos.size()); ++i) {
                if (g_state.demos[static_cast<size_t>(i)].hasImage) {
                    first = i;
                    break;
                }
            }
            if (first < 0) {
                spdlog::error("TEST FAIL: no runnable demos found");
                ++g_state.smokeFailures;
                sapp_quit();
                return;
            }
            g_state.smokeIndex = first;
            selectDemo(first);
        }
        ++g_state.smokeFrame;
    }
    ++g_state.demoFrame;

    shadertoy::FrameParams fp;
    fillFrameParams(fp);

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

    g_state.runtime.renderBuffers(fp);
    g_state.runtime.renderImageToTarget(fp);

    sg_pass_action action = {};
    action.colors[0].load_action = SG_LOADACTION_CLEAR;
    action.colors[0].clear_value = {0.07f, 0.08f, 0.10f, 1.0f};
    sg_pass pass = {};
    pass.action = action;
    pass.swapchain = sglue_swapchain();
    sg_begin_pass(&pass);

    if (g_state.runtime.cubeMode) {
        g_state.cube.draw(g_state.runtime.imageTextureView(),
            sapp_width(), sapp_height(), fp.timeSec);
    } else {
        g_state.runtime.drawImageBlit();
    }

    if (g_state.imguiOk) {
        simgui_render();
    }
    sg_end_pass();
    sg_commit();
    g_state.runtime.endFrame();

    if (g_state.shotFrame > 0) {
        --g_state.shotFrame;
        if (g_state.shotFrame == 0) {
            writeScreenshot(g_state.cli.shotPath);
            if (!g_state.cli.smoke) {
                sapp_quit();
            }
        }
    }
    if (g_state.cli.smoke && g_state.smokeFrame >= 30) {
        smokeVerdictAndAdvance();
    }
}

void cleanup() {
    g_state.cube.shutdown();
    g_state.runtime.shutdown();
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

    const float dpi = sapp_dpi_scale();
    const float fbH = static_cast<float>(sapp_height());

    bool imguiCaptures = false;
    if (g_state.imguiOk) {
        const ImGuiIO& io = ImGui::GetIO();
        imguiCaptures = io.WantCaptureMouse || io.WantCaptureKeyboard;
    }

    switch (ev->type) {
    case SAPP_EVENTTYPE_MOUSE_MOVE:
        g_state.mouse.x = ev->mouse_x * dpi;
        g_state.mouse.y = fbH - ev->mouse_y * dpi;
        break;
    case SAPP_EVENTTYPE_MOUSE_DOWN:
        if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT && !imguiCaptures) {
            g_state.mouse.down = true;
            g_state.mouse.clickX = ev->mouse_x * dpi;
            g_state.mouse.clickY = fbH - ev->mouse_y * dpi;
        }
        break;
    case SAPP_EVENTTYPE_MOUSE_UP:
        if (ev->mouse_button == SAPP_MOUSEBUTTON_LEFT) {
            g_state.mouse.down = false;
        }
        break;
    case SAPP_EVENTTYPE_KEY_DOWN:
        if (ev->key_code == SAPP_KEYCODE_R && !imguiCaptures) {
            if (g_state.runtime.reloadDemo()) {
                g_state.demoStartTime = stm_now();
                g_state.demoFrame = 0;
            }
        }
        break;
    default:
        break;
    }
}

} // namespace

int main(int argc, char* argv[]) {
    g_state.cli = parseCli(argc, argv);

    if (g_state.cli.list) {
        const std::filesystem::path root = shadertoy::resolveShadertoyRoot(g_state.cli.dir);
        const auto demos = shadertoy::scanDemos(root);
        for (const auto& demo : demos) {
            std::printf("%s%s\n", demo.name.c_str(), demo.hasImage ? "" : " (stub)");
        }
        return 0;
    }

    sapp_desc desc = {};
    desc.init_cb = init;
    desc.frame_cb = frame;
    desc.cleanup_cb = cleanup;
    desc.event_cb = event;
    desc.width = 1280;
    desc.height = 720;
    desc.sample_count = 1;
    desc.window_title = "ShadertoyPlayground";
    desc.high_dpi = true;
#if defined(_WIN32)
    desc.win32.console_utf8 = true;
    desc.win32.console_attach = true;
#endif
    desc.logger.func = logCapture;

    sapp_run(&desc);
    return g_state.smokeFailures > 0 ? 1 : 0;
}
