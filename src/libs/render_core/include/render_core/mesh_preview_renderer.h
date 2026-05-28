#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "render_core/sokol_config.h"

namespace render_core {

struct MeshPreviewQuad {
    glm::vec3 a{0.0f};
    glm::vec3 b{0.0f};
    glm::vec3 c{0.0f};
    glm::vec3 d{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec4 color{1.0f};
    glm::vec2 uvA{0.0f};
    glm::vec2 uvB{1.0f, 0.0f};
    glm::vec2 uvC{1.0f};
    glm::vec2 uvD{0.0f, 1.0f};
    float faceKind = 0.0f; // 0=top grass, 1=rock wall.
    float cliffDistance = 1000.0f;
    float relief = 0.0f;        // signed wall displacement: >0 ridge, <0 crevice.
    float heightFraction = 1.0f; // wall vertical position: 0 base, 1 top.
    float depth = 0.0f;
};

struct MeshPreviewRenderParams {
    glm::vec2 worldCenter{0.0f};
    glm::vec2 pan{0.0f};
    glm::vec3 lightDirection{-0.35f, 0.82f, -0.45f};
    float zoom = 1.0f;
    float anchorY = 130.0f;
    float isoScaleX = 19.0f;
    float isoScaleY = 9.5f;
    float heightScale = 25.0f;
    float ambient = 0.98f;
    float diffuse = 0.30f;
    float wallBrightness = 1.05f;
    float textureScale = 1.0f;
    float cliffDarkeningRadius = 1.35f;
    float cliffDarkeningStrength = 0.32f;
    float minTopBrightness = 0.68f;
    float edgeDarkness = 0.12f;
    float macroScale = 0.35f;
    float macroStrength = 0.08f;
    int debugMode = 0; // 0=lit, 1=albedo, 2=normal, 3=uv, 4=cliff proximity.
    float wallAoStrength = 0.35f;     // A3: darken wall base and crevices.
    float wallEdgeWearStrength = 0.4f; // C1: lighten/desaturate protruding facet ridges.
    float wallCreviceStrength = 0.45f; // C2: darken recessed facets (dirt/moss).
    float wallGrainStrength = 0.22f;   // B2: procedural fbm rock grain amount.
};

class MeshPreviewRenderer {
public:
    struct Vertex {
        float pos[3];
        float normal[3];
        float uv[2];
        float color[4];
        float faceKind;
        float cliffDistance;
        float wallDetail[2]; // x=relief, y=heightFraction.
    };

    void init();
    void shutdown();

    bool render(
        const std::vector<MeshPreviewQuad>& quads,
        const MeshPreviewRenderParams& params,
        sg_view grassView,
        sg_view rockView,
        sg_sampler materialSampler,
        int width,
        int height);

    bool validOutput() const;
    sg_view outputView() const { return m_outputTextureView; }
    sg_sampler outputSampler() const { return m_outputSampler; }

private:
    struct VsParams {
        float outputSize[2];
        float worldCenter[2];
        float pan[2];
        float zoom;
        float anchorY;
        float isoScale[2];
        float heightScale;
        float _pad0;
    };

    struct FsParams {
        float lightDir[4];
        float options0[4]; // ambient, diffuse, wallBrightness, textureScale.
        float options1[4]; // cliffRadius, cliffStrength, minTopBrightness, edgeDarkness.
        float options2[4]; // macroScale, macroStrength, debugMode, unused.
        float options3[4]; // wallAoStrength, wallEdgeWearStrength, wallCreviceStrength, unused.
    };

    void ensurePipeline();
    void destroyPipeline();
    void ensureTarget(int width, int height);
    void destroyTarget();
    void ensureVertexBuffer(std::size_t vertexCount);
    void destroyVertexBuffer();

    sg_pipeline m_pipeline{SG_INVALID_ID};
    sg_shader m_shader{SG_INVALID_ID};
    sg_buffer m_vertexBuffer{SG_INVALID_ID};
    sg_image m_outputImage{SG_INVALID_ID};
    sg_view m_outputAttachmentView{SG_INVALID_ID};
    sg_view m_outputTextureView{SG_INVALID_ID};
    sg_sampler m_outputSampler{SG_INVALID_ID};
    sg_bindings m_bindings{};
    std::vector<Vertex> m_vertices;
    std::size_t m_vertexCapacity = 0;
    int m_width = 0;
    int m_height = 0;
    bool m_initialized = false;
};

} // namespace render_core
