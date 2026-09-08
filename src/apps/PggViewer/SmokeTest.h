#pragma once

#include <string>

class ViewerRpcServer;

// Registers the --serve RPC command handlers (implemented in main.cpp) on
// `server`; the smoke test drives the same handlers on its own instance.
void registerPggViewerRpcHandlers(ViewerRpcServer& server);

// CPU smoke test of the viewer (--smoke), run before any sokol init: graph
// derivation on the pgg corpus (tower instances + dive targets, foreach zone
// ports and the state loop), the instance-numbering cross-check against
// FlatProgram, layout determinism and the hint parse/write-back round-trip.
// With serveAddress non-empty (--serve[=host:port] --smoke) an extra block
// covers the RPC server: a real server + an in-process socket client driven
// by a manual poll() loop (ping/status/load/probe, and render failing
// headless with no_frame_loop).
// Logs TEST PASS / TEST FAIL lines; returns true when everything passed.
bool runPggViewerSmokeTest(const std::string& serveAddress = {});
