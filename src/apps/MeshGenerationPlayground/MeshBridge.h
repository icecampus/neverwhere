#pragma once

#include "LandscapeBowlScenario.h"
#include "PlaygroundTypes.h"

#include <cstdint>
#include <vector>

#include <landscape_core/landscape_logic.h>
#include <landscape_mesh/landscape_mesh.h>

namespace meshgen_playground {

LandscapeZone toLocalZone(landscape_core::LandscapeZone zone);
landscape_core::LandscapeBowlSettings toCoreLandscapeSettings(const LandscapeBowlSettings& settings);
MeshQuad toAppMeshQuad(const landscape_mesh::MeshQuad& quad);
landscape_mesh::SolidMaskGrid toSharedSolidMaskGrid(
    const std::vector<std::uint8_t>& solidCells,
    int width,
    int height);
landscape_mesh::MeshBuildSettings makeSharedLandscapeMeshSettings(float levelHeight);

} // namespace meshgen_playground
