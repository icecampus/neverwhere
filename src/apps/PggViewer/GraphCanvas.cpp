#include "pch.h"

#include "GraphCanvas.h"

#include <cmath>

namespace {

ImU32 col(int r, int g, int b, int a = 255) { return IM_COL32(r, g, b, a); }

// Canvas text scales with the zoom (fixed-size ImGui text is what made
// zoomed-out graphs unreadable); below the legibility threshold it is skipped.
void canvasText(ImDrawList* dl, ImVec2 pos, ImU32 c, const char* text, float zoom, float base = 13.0f) {
    const float fs = base * zoom;
    if (fs < 6.0f || !text || text[0] == '\0') return;
    dl->AddText(ImGui::GetFont(), fs, pos, c, text);
}

std::string nodeTitle(const pgg::GraphNode& n) {
    if (!n.name.empty()) return n.name;
    std::string t;
    for (size_t i = 0; i < n.outputs.size(); ++i) t += (i ? ", " : "") + n.outputs[i];
    return t;
}

ImVec2 outPinPos(const pgg::GraphNode& n, int pin, const pgg::LayoutParams& params) {
    const ImVec4 r = graphNodeRect(n, params);
    const int clamped = std::clamp(pin, 0, std::max<int>(0, static_cast<int>(n.outputs.size()) - 1));
    return {r.z, r.y + params.headerH + (clamped + 0.5f) * params.pinRowH};
}

ImVec2 inPinPos(const pgg::GraphNode& n, int pin, const pgg::LayoutParams& params) {
    const ImVec4 r = graphNodeRect(n, params);
    const int clamped = std::clamp(pin, 0, std::max<int>(0, static_cast<int>(n.inputs.size()) - 1));
    return {r.x, r.y + params.headerH + (clamped + 0.5f) * params.pinRowH};
}

ImU32 nodeBg(pgg::GraphNode::Kind kind) {
    switch (kind) {
        case pgg::GraphNode::Kind::DefCall: return col(66, 54, 34);
        case pgg::GraphNode::Kind::Param: return col(38, 62, 44);
        case pgg::GraphNode::Kind::Output: return col(64, 44, 40);
        case pgg::GraphNode::Kind::Import:
        case pgg::GraphNode::Kind::Tap: return col(48, 48, 56);
        case pgg::GraphNode::Kind::ZoneHeader: return col(56, 44, 66);
        case pgg::GraphNode::Kind::ZoneInput:
        case pgg::GraphNode::Kind::ZoneOutput: return col(40, 58, 60);
        default: return col(44, 54, 70);
    }
}

}  // namespace

ImVec4 graphNodeRect(const pgg::GraphNode& n, const pgg::LayoutParams& params) {
    const size_t rows = std::max<size_t>({n.inputs.size(), n.outputs.size(), 1});
    const float h = params.headerH + static_cast<float>(rows) * params.pinRowH;
    return {n.x - params.nodeW * 0.5f, n.y - h * 0.5f, n.x + params.nodeW * 0.5f, n.y + h * 0.5f};
}

void canvasFitView(const pgg::GraphScope& scope, const pgg::LayoutParams& params, GraphCanvasState& cs,
                   float viewW, float viewH) {
    float minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
    for (const pgg::GraphNode& n : scope.nodes) {
        const ImVec4 r = graphNodeRect(n, params);
        minX = std::min(minX, r.x);
        minY = std::min(minY, r.y);
        maxX = std::max(maxX, r.z);
        maxY = std::max(maxY, r.w);
    }
    for (const pgg::GraphZone& z : scope.zones) {
        minX = std::min(minX, z.x);
        minY = std::min(minY, z.y);
        maxX = std::max(maxX, z.x + z.w);
        maxY = std::max(maxY, z.y + z.h);
    }
    if (minX > maxX) {
        cs.offsetX = 60.0f;
        cs.offsetY = 60.0f;
        cs.zoom = 1.0f;
        return;
    }
    const float margin = 60.0f;
    const float bw = maxX - minX + 2.0f * margin;
    const float bh = maxY - minY + 2.0f * margin;
    cs.zoom = std::clamp(std::min(viewW / bw, viewH / bh), 0.1f, 1.5f);
    cs.offsetX = (viewW - (maxX - minX) * cs.zoom) * 0.5f - minX * cs.zoom;
    cs.offsetY = (viewH - (maxY - minY) * cs.zoom) * 0.5f - minY * cs.zoom;
}

GraphCanvasResult drawGraphCanvas(pgg::GraphScope& scope, const pgg::LayoutParams& params,
                                  GraphCanvasState& cs, bool editable) {
    GraphCanvasResult res;
    ImGuiIO& io = ImGui::GetIO();
    const ImVec2 winPos = ImGui::GetWindowPos();
    const ImVec2 winSize = ImGui::GetWindowSize();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::InvisibleButton("##graphcanvas", winSize,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool itemHovered = ImGui::IsItemHovered();

    auto w2s = [&](float x, float y) {
        return ImVec2(winPos.x + cs.offsetX + x * cs.zoom, winPos.y + cs.offsetY + y * cs.zoom);
    };
    const ImVec2 mouseWorld{(io.MousePos.x - winPos.x - cs.offsetX) / cs.zoom,
                            (io.MousePos.y - winPos.y - cs.offsetY) / cs.zoom};

    // --- input -------------------------------------------------------------------

    if (itemHovered && io.MouseWheel != 0.0f) {
        const float factor = io.MouseWheel > 0.0f ? 1.12f : 1.0f / 1.12f;
        const float newZoom = std::clamp(cs.zoom * factor, 0.08f, 4.0f);
        // Zoom to the cursor: keep the world point under the mouse fixed.
        cs.offsetX = (io.MousePos.x - winPos.x) - mouseWorld.x * newZoom;
        cs.offsetY = (io.MousePos.y - winPos.y) - mouseWorld.y * newZoom;
        cs.zoom = newZoom;
    }

    auto hitNode = [&]() -> int {
        for (size_t i = scope.nodes.size(); i-- > 0;) {
            const ImVec4 r = graphNodeRect(scope.nodes[i], params);
            if (mouseWorld.x >= r.x && mouseWorld.x <= r.z && mouseWorld.y >= r.y && mouseWorld.y <= r.w)
                return static_cast<int>(i);
        }
        return -1;
    };
    cs.hovered = itemHovered ? hitNode() : -1;

    if (itemHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        cs.dragNode = cs.hovered;
        cs.dragMoved = false;
        cs.dragMouseStartX = mouseWorld.x;
        cs.dragMouseStartY = mouseWorld.y;
        if (cs.dragNode >= 0) {
            cs.dragNodeStartX = scope.nodes[cs.dragNode].x;
            cs.dragNodeStartY = scope.nodes[cs.dragNode].y;
            if (cs.selected != cs.dragNode) {
                cs.selected = cs.dragNode;
                res.selectionChanged = true;
            }
        } else if (cs.selected != -1) {
            cs.selected = -1;
            res.selectionChanged = true;
        }
    }
    if (itemHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && cs.hovered >= 0 &&
        scope.nodes[cs.hovered].kind == pgg::GraphNode::Kind::DefCall) {
        res.diveNode = cs.hovered;
    }
    if (ImGui::IsItemActive() && cs.dragNode >= 0 && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        pgg::GraphNode& n = scope.nodes[cs.dragNode];
        n.x = cs.dragNodeStartX + (mouseWorld.x - cs.dragMouseStartX);
        n.y = cs.dragNodeStartY + (mouseWorld.y - cs.dragMouseStartY);
        cs.dragMoved = true;
    } else if (ImGui::IsItemActive() && (ImGui::IsMouseDragging(ImGuiMouseButton_Left) ||
                                         ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
        cs.offsetX += io.MouseDelta.x;
        cs.offsetY += io.MouseDelta.y;
    }
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && cs.dragNode >= 0) {
        if (cs.dragMoved && editable) res.hintNode = cs.dragNode;
        cs.dragNode = -1;
        cs.dragMoved = false;
    }

    // --- drawing -------------------------------------------------------------------

    dl->AddRectFilled(winPos, ImVec2(winPos.x + winSize.x, winPos.y + winSize.y), col(24, 27, 32));

    // Zone frames, outermost first (zone ids are created parent-first).
    for (const pgg::GraphZone& z : scope.zones) {
        const ImVec2 a = w2s(z.x, z.y);
        const ImVec2 b = w2s(z.x + z.w, z.y + z.h);
        dl->AddRectFilled(a, b, col(38, 42, 52, 160), 8.0f * cs.zoom);
        dl->AddRect(a, b, col(96, 104, 130), 8.0f * cs.zoom, 0, 1.5f);
    }

    // Wires under the nodes.
    for (const pgg::GraphEdge& e : scope.edges) {
        const ImVec2 p0w = outPinPos(scope.nodes[e.fromNode], e.fromPin, params);
        const ImVec2 p1w = inPinPos(scope.nodes[e.toNode], e.toPin, params);
        const ImVec2 p0 = w2s(p0w.x, p0w.y);
        const ImVec2 p1 = w2s(p1w.x, p1w.y);
        const float bend = std::max(40.0f * cs.zoom, std::fabs(p1.x - p0.x) * 0.5f);
        const ImU32 c = e.loop ? col(226, 158, 62) : col(132, 148, 176);
        dl->AddBezierCubic(p0, ImVec2(p0.x + bend, p0.y), ImVec2(p1.x - bend, p1.y), p1, c, 1.6f);
        if (e.loop) {
            const ImVec2 mid{(p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f};
            canvasText(dl, ImVec2(mid.x + 4.0f, mid.y - 12.0f * cs.zoom), c, "loop", cs.zoom, 11.0f);
        }
    }

    // Nodes.
    for (const pgg::GraphNode& n : scope.nodes) {
        const ImVec4 rw = graphNodeRect(n, params);
        const ImVec2 a = w2s(rw.x, rw.y);
        const ImVec2 b = w2s(rw.z, rw.w);
        const float rounding = 6.0f * cs.zoom;
        dl->AddRectFilled(a, b, nodeBg(n.kind), rounding);
        ImU32 border = col(96, 106, 128);
        float thickness = 1.2f;
        if (n.id == cs.selected) {
            border = col(238, 212, 130);
            thickness = 2.2f;
        } else if (n.id == cs.hovered) {
            border = col(160, 172, 198);
        }
        dl->AddRect(a, b, border, rounding, 0, thickness);
        if (n.kind == pgg::GraphNode::Kind::DefCall) {
            // The collapsed-def marker: a second outline and the instance tag.
            dl->AddRect(ImVec2(a.x - 3.0f * cs.zoom, a.y - 3.0f * cs.zoom),
                        ImVec2(b.x + 3.0f * cs.zoom, b.y + 3.0f * cs.zoom), col(196, 158, 84), rounding,
                        0, 1.2f);
        }
        if (n.hasHint) {
            dl->AddCircleFilled(ImVec2(b.x - 7.0f * cs.zoom, a.y + 7.0f * cs.zoom), 2.5f * cs.zoom,
                                col(120, 190, 240));
        }

        dl->PushClipRect(a, b, true);
        const float pad = 8.0f * cs.zoom;
        const std::string title = nodeTitle(n);
        canvasText(dl, ImVec2(a.x + pad, a.y + 5.0f * cs.zoom), col(232, 236, 244), title.c_str(),
                   cs.zoom);
        canvasText(dl, ImVec2(a.x + pad, a.y + 22.0f * cs.zoom), col(146, 152, 166), n.op.c_str(),
                   cs.zoom, 12.0f);

        for (size_t i = 0; i < n.inputs.size(); ++i) {
            const ImVec2 pw = inPinPos(n, static_cast<int>(i), params);
            const ImVec2 p = w2s(pw.x, pw.y);
            dl->AddCircleFilled(p, 3.5f * cs.zoom, col(126, 196, 156));
            if (!n.inputs[i].empty())
                canvasText(dl, ImVec2(p.x + 8.0f * cs.zoom, p.y - 6.0f * cs.zoom), col(150, 168, 158),
                           n.inputs[i].c_str(), cs.zoom, 11.0f);
        }
        for (size_t i = 0; i < n.outputs.size(); ++i) {
            const ImVec2 pw = outPinPos(n, static_cast<int>(i), params);
            const ImVec2 p = w2s(pw.x, pw.y);
            dl->AddCircleFilled(p, 3.5f * cs.zoom, col(126, 156, 206));
            const float fs = 11.0f * cs.zoom;
            const float tw = fs >= 6.0f
                                 ? ImGui::GetFont()->CalcTextSizeA(fs, FLT_MAX, 0.0f, n.outputs[i].c_str()).x
                                 : 0.0f;
            canvasText(dl, ImVec2(p.x - 8.0f * cs.zoom - tw, p.y - 6.0f * cs.zoom), col(150, 158, 186),
                       n.outputs[i].c_str(), cs.zoom, 11.0f);
        }
        dl->PopClipRect();
    }
    return res;
}
