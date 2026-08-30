#pragma once

// CPU smoke test of the viewer (--smoke), run before any sokol init: graph
// derivation on the pgg corpus (tower instances + dive targets, foreach zone
// ports and the state loop), the instance-numbering cross-check against
// FlatProgram, layout determinism and the hint parse/write-back round-trip.
// Logs TEST PASS / TEST FAIL lines; returns true when everything passed.
bool runPggViewerSmokeTest();
