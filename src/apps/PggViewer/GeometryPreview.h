// Geometry preview (spec §9 L3 groundwork): renders the value of a selected
// node — geo<mesh>, geo<points>, geo<instances> (realized) or sdf (meshed at
// a preview voxel) — into an offscreen sokol target shown in an ImGui window
// with an orbit camera. The value comes from a RunParams::pulls run (any
// binding, not only declared outputs). Optional highlight of one group.
//
// Frame contract: buildFrom() at load/selection time (CPU); drawWindow()
// inside the ImGui frame (handles input, may recreate the render target on
// resize); render() OUTSIDE any sokol pass, before the swapchain pass that
// draws ImGui (an offscreen pass cannot be nested in the swapchain pass).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <sokol_gfx.h>

#include <pgg/src/eval/value.h>

struct PreviewVertex {
    glm::vec3 pos;
    glm::vec3 normal;
    float mask;  // 1 = in the highlighted group
};

struct PreviewGeometry {
    std::vector<PreviewVertex> vertices;
    std::vector<uint32_t> indices;
    glm::vec3 bmin{0.0f}, bmax{0.0f};
    std::string summary;                 // "mesh 12508 pts, 25004 tri" / "sdf -> mesh ..." / error text
    std::vector<std::string> groups;     // highlightable group names ("<domain>:<name>")
    bool ok = false;
};

struct PreviewBuildOptions {
    std::string highlightGroup;  // "<domain>:<name>" from PreviewGeometry::groups, "" = none
    int sdfResolution = 64;      // longest bbox axis in voxels for sdf values
    unsigned threads = 0;
};

// Converts a runtime value into render-ready triangles (CPU only, no sokol).
PreviewGeometry buildPreviewGeometry(const pgg::Value& value, const PreviewBuildOptions& opts);

class GeometryPreview {
public:
    void init();
    void shutdown();

    // Uploads new geometry and refits the camera (keeps the orbit angles).
    void setGeometry(const PreviewGeometry& geo, bool refit);
    void clear();
    bool hasGeometry() const { return m_indexCount > 0; }

    // ImGui window body (call between simgui_new_frame and the swapchain pass).
    // Draws the image, orbit/pan/zoom on hover, and a status line.
    void drawWindowContents();
    // Offscreen pass. No-op without a target (window never shown).
    void render();

    const std::string& summary() const { return m_summary; }
    void setSummary(const std::string& s) { m_summary = s; }
    // Run error shown wrapped in red over the canvas (empty = none).
    void setError(const std::string& s) { m_error = s; }

private:
    struct VsParams {
        float mvp[16];
    };
    struct FsParams {
        float lightDir[4];
        float highlight[4];  // rgb + strength
        float base[4];       // rgb + flat-shade flag
    };

    void ensureTarget(int w, int h);
    void destroyTarget();
    glm::mat4 viewProj(float aspect) const;

    sg_shader m_shader{};
    sg_pipeline m_pip{};
    sg_buffer m_vbuf{};
    sg_buffer m_ibuf{};
    int m_indexCount = 0;

    sg_image m_color{};
    sg_image m_depth{};
    sg_view m_colorAttach{};
    sg_view m_depthAttach{};
    sg_view m_texView{};
    int m_targetW = 0, m_targetH = 0;
    int m_wantW = 0, m_wantH = 0;

    // Orbit camera.
    glm::vec3 m_center{0.0f};
    glm::vec3 m_fitCenter{0.0f};
    float m_radius = 1.0f;      // fit radius of the geometry
    float m_distance = 3.0f;
    float m_yaw = 0.6f;
    float m_pitch = 0.5f;

    std::string m_summary;
    std::string m_error;
    bool m_ok = false;
};
