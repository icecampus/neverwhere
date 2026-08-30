#pragma once

// The node-graph canvas of PggViewer (spec §10 MVP): draws one GraphScope
// with ImDrawList — zone frames with title strips, bezier wires, node boxes
// with in/out pin rows — and owns the interaction: pan (drag empty space or
// RMB), zoom to cursor (wheel), node drag with layout-hint write-back on
// release, double-click dive into def-call nodes, selection for the probe
// panel. No new dependencies on purpose (no imnodes).

#include <imgui.h>

#include <pgg/src/graph.h>
#include <pgg/src/layout.h>

struct GraphCanvasState {
    float offsetX = 60.0f;  // world -> screen translation, window-local points
    float offsetY = 60.0f;
    float zoom = 1.0f;
    int selected = -1;   // node id in the current scope (-1 = none)
    int hovered = -1;
    // Active node drag (LMB on a node).
    int dragNode = -1;
    float dragNodeStartX = 0.0f;
    float dragNodeStartY = 0.0f;
    float dragMouseStartX = 0.0f;
    float dragMouseStartY = 0.0f;
    bool dragMoved = false;
};

struct GraphCanvasResult {
    int diveNode = -1;        // double-clicked def-call node
    int hintNode = -1;        // node whose drag ended -> write its hint
    bool selectionChanged = false;
};

// Draws the scope into the current ImGui window (the window is the canvas)
// and processes input. `editable` enables hint write-back on drags (only
// main-file scopes are editable; module bodies are read-only views).
GraphCanvasResult drawGraphCanvas(pgg::GraphScope& scope, const pgg::LayoutParams& params,
                                  GraphCanvasState& cs, bool editable);

// Frames the scope content in the current camera (used on load and dive).
void canvasFitView(const pgg::GraphScope& scope, const pgg::LayoutParams& params, GraphCanvasState& cs,
                   float viewW, float viewH);

// World-space rect (x0, y0, x1, y1) of a node — shared by drawing, hit-testing
// and wire endpoints.
ImVec4 graphNodeRect(const pgg::GraphNode& n, const pgg::LayoutParams& params);
