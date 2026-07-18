#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#if !defined(SOKOL_D3D11) && !defined(SOKOL_METAL) && !defined(SOKOL_GLES3) && !defined(SOKOL_GLCORE)
    #if defined(_WIN32)
        #define SOKOL_D3D11
    #elif defined(__APPLE__)
        #define SOKOL_METAL
    #elif defined(__EMSCRIPTEN__)
        #define SOKOL_GLES3
    #else
        #define SOKOL_GLCORE
    #endif
#endif

#include <sokol_gfx.h>

#include "DiamondIso.h"
#include "LandBrush.h"
#include <topology_core/camera2d.h>

enum class AtlasKind : int {
    Grass = 0,
    Flat = 1,
};

class AtlasRenderer {
public:
    struct VsParams {
        float view_size[2];
        float camera_offset[2];
        float camera_zoom;
        float _pad[3];
    };

    void init();
    void shutdown();

    bool loadAtlasFromFile(AtlasKind kind, const std::string& path, int cols = 4, int rows = 6);
    bool loadAtlasFromRgba(
        AtlasKind kind,
        const std::uint8_t* rgba,
        int width,
        int height,
        int cols = 4,
        int rows = 6);

    void setActiveAtlas(AtlasKind kind);
    AtlasKind activeAtlas() const { return m_active; }

    void render(
        const LandBrush& brush,
        const LandBrush& raisedBrush,
        const DiamondIso& iso,
        const topology_core::Camera2D& camera,
        int viewW,
        int viewH,
        glm::ivec2 hoverNode,
        bool hasHover,
        float raisedHeight,
        bool hoverRaised);

private:
    struct AtlasSlot {
        sg_image image{};
        sg_view view{};
    };

    struct TexVertex {
        float x, y;
        float u, v;
    };

    struct ColorVertex {
        float x, y;
        float r, g, b, a;
    };

    void ensurePipelines();
    void destroyPipelines();
    void destroySlot(AtlasSlot& slot);
    bool uploadSlot(
        AtlasSlot& slot,
        const void* rgba,
        int width,
        int height,
        const char* label);
    glm::vec4 atlasUvRect(int tileIndex) const;
    void appendTileQuad(
        std::vector<TexVertex>& out,
        const DiamondIso& iso,
        glm::ivec2 cell,
        int tileIndex,
        float yOffset = 0.0f);
    void appendWallTriangles(
        std::vector<ColorVertex>& out,
        const DiamondIso& iso,
        glm::ivec2 cell,
        const std::array<bool, 4>& mask,
        float height);
    void appendDiamondOutline(std::vector<ColorVertex>& out, const DiamondIso& iso, glm::ivec2 cell, glm::vec4 color);
    void appendNodeMarker(
        std::vector<ColorVertex>& out,
        const DiamondIso& iso,
        glm::ivec2 node,
        glm::vec4 color,
        float yOffset = 0.0f);

    sg_pipeline m_texPip{};
    sg_pipeline m_colorPip{};
    sg_pipeline m_wallPip{};
    sg_shader m_texShd{};
    sg_shader m_colorShd{};
    sg_buffer m_texVbuf{};
    sg_buffer m_colorVbuf{};
    sg_sampler m_sampler{};

    AtlasSlot m_slots[2]{};
    AtlasKind m_active = AtlasKind::Grass;

    int m_atlasCols = 4;
    int m_atlasRows = 6;
    bool m_ready = false;
};
