#include "PlaygroundVisualCapture.h"

#include "MeshPreview.h"
#include "PlaygroundScreenshot.h"
#include "PlaygroundState.h"

#include <filesystem>
#include <mutex>
#include <string>

#include <sokol_app.h>
#include <spdlog/spdlog.h>

namespace meshgen_playground {

namespace {

VisualCaptureOptions g_visualCaptureOptions;
int g_visualCaptureStep = 0;
bool g_visualCaptureFinished = false;
bool g_visualCaptureBlockGpuPreview = false;

void applyCaptureDebugMode(ProductionPreviewDebugMode mode) {
    std::lock_guard<std::mutex> lock(g_stateMutex);
    g_productionPreviewSettings.useGpuRenderer = true;
    g_productionPreviewSettings.showEnvSprites = false;
    g_productionPreviewSettings.debugMode = (int)mode;
    g_landscapeCamera.zoom = 1.0f;
    g_landscapeCamera.pan = {0.0f, 0.0f};
}

std::filesystem::path capturePath(const char* filename) {
    return std::filesystem::path(g_visualCaptureOptions.outputDir) / filename;
}

} // namespace

bool parseVisualCaptureArgs(int argc, char** argv, VisualCaptureOptions& options) {
    for (int i = 1; i < argc; i++) {
        const std::string arg = argv[i] ? argv[i] : "";
        if (arg == "--capture-visuals") {
            options.enabled = true;
            if (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '-') {
                options.outputDir = argv[i + 1];
                i++;
            }
            return true;
        }
        if (arg.rfind("--capture-visuals=", 0) == 0) {
            options.enabled = true;
            options.outputDir = arg.substr(std::string("--capture-visuals=").size());
            return true;
        }
    }
    return false;
}

bool visualCaptureEnabled() {
    return g_visualCaptureOptions.enabled && !g_visualCaptureFinished;
}

bool visualCaptureBlockGpuPreview() {
    return g_visualCaptureBlockGpuPreview;
}

const VisualCaptureOptions& visualCaptureOptions() {
    return g_visualCaptureOptions;
}

void beginVisualCapture(const VisualCaptureOptions& options) {
    g_visualCaptureOptions = options;
    g_visualCaptureStep = 0;
    g_visualCaptureFinished = false;
    std::filesystem::create_directories(g_visualCaptureOptions.outputDir);
    spdlog::info("visual capture: output directory={}", g_visualCaptureOptions.outputDir);
}

bool updateVisualCaptureBeforeFrame(int frameIndex) {
    g_visualCaptureBlockGpuPreview = false;
    if (!g_visualCaptureOptions.enabled || g_visualCaptureFinished || frameIndex < 4) {
        return false;
    }

    bool ok = true;
    switch (g_visualCaptureStep) {
    case 0:
        applyCaptureDebugMode(ProductionPreviewDebugMode::Lit);
        g_visualCaptureStep++;
        return false;
    case 1:
        ok = captureProductionPreviewGpuPng(capturePath("lit_gpu.png").string().c_str(), ProductionPreviewDebugMode::Lit);
        g_visualCaptureBlockGpuPreview = true;
        g_visualCaptureStep++;
        return ok;
    case 4:
        applyCaptureDebugMode(ProductionPreviewDebugMode::NormalVectors);
        g_visualCaptureStep++;
        return false;
    case 7:
        applyCaptureDebugMode(ProductionPreviewDebugMode::StableNormals);
        g_visualCaptureStep++;
        return false;
    case 8:
        ok = captureProductionPreviewGpuPng(
            capturePath("stable_normals_debug.png").string().c_str(),
            ProductionPreviewDebugMode::StableNormals);
        g_visualCaptureBlockGpuPreview = true;
        g_visualCaptureStep++;
        return ok;
    default:
        return false;
    }
}

bool updateVisualCaptureAfterFrame(int frameIndex) {
    if (!g_visualCaptureOptions.enabled || g_visualCaptureFinished) {
        return false;
    }

    if (frameIndex < 3) {
        return false;
    }

    bool ok = true;
    switch (g_visualCaptureStep) {
    case 2:
        ok = captureWindowClientPng(capturePath("lit_window.png").string().c_str());
        g_visualCaptureStep++;
        return false;
    case 3:
        // One present cycle after Lit window capture before switching debug modes.
        g_visualCaptureStep++;
        return false;
    case 5:
        // Settle frame after NormalVectors mode is applied.
        g_visualCaptureStep++;
        return false;
    case 6:
        ok = captureWindowClientPng(capturePath("normal_vectors.png").string().c_str());
        g_visualCaptureStep++;
        return false;
    case 9:
        // Settle frame after StableNormals GPU capture.
        g_visualCaptureStep++;
        return false;
    case 10:
        ok = captureWindowClientPng(capturePath("stable_normals_window.png").string().c_str());
        if (ok) {
            spdlog::info("TEST PASS visual capture: wrote screenshots to {}", g_visualCaptureOptions.outputDir);
        } else {
            spdlog::error("TEST FAIL visual capture: one or more screenshots failed in {}", g_visualCaptureOptions.outputDir);
        }
        g_visualCaptureFinished = true;
        sapp_quit();
        return true;
    default:
        return false;
    }
}

bool visualCaptureFinished() {
    return g_visualCaptureFinished;
}

} // namespace meshgen_playground
