#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include <render_core/mesh_preview_renderer.h>

#include "LandscapeCellCatalog.h"
#include "LandscapeModel.h"

namespace landscape3d {

struct LandscapePreviewSettings {
    float zoom = 1.0f;
    glm::vec2 pan{0.0f};
    glm::vec3 lightDirection{-0.35f, 0.82f, -0.45f};
    float ambient = 0.62f;
    float diffuseStrength = 0.70f;
    float wallBrightness = 1.08f;
    float textureScale = 1.0f;
    float cliffDarkeningRadius = 1.35f;
    float cliffDarkeningStrength = 0.22f;
    float minTopBrightness = 0.78f;
    float edgeDarkness = 0.08f;
    float wallAoStrength = 0.35f;
    float wallEdgeWearStrength = 0.22f;
    float wallCreviceStrength = 0.24f;
    float wallGrainStrength = 0.22f;
};

class LandscapePreview {
public:
    void init();
    void shutdown();

    bool render(
        const LandscapeModel& model,
        const LandscapeCellCatalog& catalog,
        const LandscapePreviewSettings& settings,
        int width,
        int height);

    std::optional<GridPoint> pickNode(
        const LandscapeModel& model,
        const LandscapePreviewSettings& settings,
        glm::vec2 viewportSize,
        glm::vec2 cursorPosition) const;
    glm::vec2 projectNode(
        const LandscapeModel& model,
        const LandscapePreviewSettings& settings,
        glm::vec2 viewportSize,
        GridPoint node) const;

    bool validOutput() const { return m_renderer.validOutput(); }
    sg_view outputView() const { return m_renderer.outputView(); }
    sg_sampler outputSampler() const { return m_renderer.outputSampler(); }
    int renderedQuadCount() const { return m_renderedQuadCount; }
    bool grassTextureLoaded() const { return m_grassLoaded; }
    bool rockTextureLoaded() const { return m_rockLoaded; }

private:
    struct Texture {
        sg_image image{SG_INVALID_ID};
        sg_view view{SG_INVALID_ID};
    };

    void initTextures();
    void destroyTexture(Texture& texture);
    bool loadTexture(const std::filesystem::path& path, const std::array<std::uint8_t, 16>& fallback, Texture& texture);
    std::vector<render_core::MeshPreviewQuad> buildRenderQuads(
        const LandscapeModel& model,
        const LandscapeCellCatalog& catalog) const;
    render_core::MeshPreviewRenderParams makeRenderParams(
        const LandscapeModel& model,
        const LandscapePreviewSettings& settings,
        glm::vec2 viewportSize) const;

    render_core::MeshPreviewRenderer m_renderer;
    Texture m_grass;
    Texture m_rock;
    sg_sampler m_materialSampler{SG_INVALID_ID};
    int m_renderedQuadCount = 0;
    float m_highgroundHeight = 1.6f;
    bool m_grassLoaded = false;
    bool m_rockLoaded = false;
};

} // namespace landscape3d
