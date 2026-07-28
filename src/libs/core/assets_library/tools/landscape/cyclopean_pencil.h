#pragma once
#include "assets_library/tools/landscape/landscape_pencil.h"

// Pencil for CyclopeanAsset — same vertex-centric node flip as
// LandscapePencil; writes cyclopean Landscape tiles to the CyclopeanLandscape
// layer (the target layer is resolved from the asset's layerType).
class CyclopeanPencil: public LandscapePencil
{
public:
    CyclopeanPencil(QObject* parent);
};
