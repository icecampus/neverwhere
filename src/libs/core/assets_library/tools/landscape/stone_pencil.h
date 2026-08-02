#pragma once
#include "assets_library/tools/landscape/landscape_pencil.h"

// Pencil for StoneAsset — same vertex-centric node flip as LandscapePencil;
// writes stone Landscape tiles to the StoneLandscape layer (the target layer
// is resolved from the asset's layerType).
class StonePencil: public LandscapePencil
{
public:
    StonePencil(QObject* parent);
};
