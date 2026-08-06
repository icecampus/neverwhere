#pragma once
#include "assets_library/tools/landscape/landscape_pencil.h"

// Pencil for TechAsset — same vertex-centric node flip as LandscapePencil;
// writes tech Landscape tiles to the TechLandscape layer (the target layer
// is resolved from the asset's layerType).
class TechPencil: public LandscapePencil
{
public:
    TechPencil(QObject* parent);
};
