#pragma once

#include <string>

namespace meshgen_playground {

struct VisualCaptureOptions {
    bool enabled = false;
    std::string outputDir = "tools/visual_tests/captures";
};

bool parseVisualCaptureArgs(int argc, char** argv, VisualCaptureOptions& options);
bool visualCaptureEnabled();
bool visualCaptureBlockGpuPreview();
const VisualCaptureOptions& visualCaptureOptions();
void beginVisualCapture(const VisualCaptureOptions& options);
bool updateVisualCaptureBeforeFrame(int frameIndex);
bool updateVisualCaptureAfterFrame(int frameIndex);
bool visualCaptureFinished();

} // namespace meshgen_playground
