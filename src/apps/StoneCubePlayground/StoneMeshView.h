// Cheap textured-mesh view for the baked stone cube: VB/IB + albedo(rgb)+AO(a)
// and object-space normal textures — the "fast render" counterpart of the
// raymarch view (same orbit camera).
#pragma once

#include "StoneCubeScene.h" // Camera + sokol backend-ensure + sokol_gfx

#include <stone_gen/stone_mesh.h>

namespace stonecube {

class StoneMeshView {
public:
    void init();
    void shutdown();

    void setMesh(const stone_gen::StoneMesh& mesh);
    void setTextures(int size, const std::vector<std::uint8_t>& albedo,
        const std::vector<std::uint8_t>& normal);

    // Draws in the CURRENT pass (swapchain pass expected).
    void draw(const Camera& cam, const float lightDir[3], int fbWidth, int fbHeight) const;

    bool hasMesh() const { return m_indexCount > 0; }
    int triCount() const { return m_indexCount / 3; }

private:
    struct VsUniforms {
        float mvp[16];
    };
    struct FsUniforms {
        float lightDir[4];
    };

    sg_buffer m_vbuf{};
    sg_buffer m_ibuf{};
    sg_shader m_shader{};
    sg_pipeline m_pip{};
    sg_sampler m_sampler{};
    sg_image m_albedoImg{};
    sg_view m_albedoView{};
    sg_image m_normalImg{};
    sg_view m_normalView{};
    int m_indexCount = 0;
};

} // namespace stonecube
