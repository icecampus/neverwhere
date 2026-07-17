#pragma once

#include <array>
#include <vector>

#include <landscape_core/landscape_logic.h>
#include <landscape_mesh/landscape_mesh.h>

namespace landscape3d {

struct LandscapeCellTemplate {
    landscape_core::LandscapeTileType type = landscape_core::LandscapeTileType::Unknown;
    std::array<bool, 4> nodeMask{};
    std::vector<landscape_mesh::MeshQuad> quads;
    int topQuadCount = 0;
    int wallQuadCount = 0;
};

struct LandscapeCellCatalogSettings {
    float highgroundHeight = 1.6f;
    int wallHorizontalSubdivisions = 5;
    int wallVerticalSubdivisions = 6;
};

class LandscapeCellCatalog {
public:
    void rebuild(const LandscapeCellCatalogSettings& settings);

    const LandscapeCellTemplate& templateFor(landscape_core::LandscapeTileType type) const;
    const LandscapeCellCatalogSettings& settings() const { return m_settings; }
    bool valid() const { return m_valid; }

private:
    LandscapeCellTemplate buildTemplate(
        landscape_core::LandscapeTileType type,
        const std::array<bool, 4>& mask) const;

    LandscapeCellCatalogSettings m_settings;
    std::array<LandscapeCellTemplate, 16> m_templates{};
    bool m_valid = false;
};

} // namespace landscape3d
