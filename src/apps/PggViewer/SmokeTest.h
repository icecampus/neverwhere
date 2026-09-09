#pragma once

#include <cstdint>
#include <string>
#include <vector>

class ViewerRpcServer;

// Registers the --serve RPC command handlers (implemented in main.cpp) on
// `server`; the smoke test drives the same handlers on its own instance.
void registerPggViewerRpcHandlers(ViewerRpcServer& server);

// Screenshot pixel crop (F1, implemented in main.cpp): copies the rect
// (x, y, w, h) — top-down framebuffer pixels — out of an RGBA8 buffer,
// clamped to the buffer bounds; false when the clamped rect is empty.
// Declared here so the smoke test can drive it on a synthetic buffer.
bool cropShotPixels(const std::vector<std::uint8_t>& src, int srcW, int srcH, int x, int y, int w, int h,
                    std::vector<std::uint8_t>& out, int& outW, int& outH);

// CPU smoke test of the viewer (--smoke), run before any sokol init: graph
// derivation on the pgg corpus (tower instances + dive targets, foreach zone
// ports and the state loop), the instance-numbering cross-check against
// FlatProgram, layout determinism and the hint parse/write-back round-trip.
// Also headless camera math (setTarget/ortho, F2 setZoom/setDistance), the
// F1 screenshot crop (cropShotPixels) on a synthetic buffer and the F3 CPU
// helpers (FrameCompare.h: compareFrames / silhouetteMetrics /
// estimateBackground / composeSideBySide) on synthetic buffers.
// With serveAddress non-empty (--serve[=host:port] --smoke) an extra block
// covers the RPC server: a real server + an in-process socket client driven
// by a manual poll() loop (ping/status/load/probe, and render/compare/
// reference failing headless with no_frame_loop).
// Logs TEST PASS / TEST FAIL lines; returns true when everything passed.
bool runPggViewerSmokeTest(const std::string& serveAddress = {});
