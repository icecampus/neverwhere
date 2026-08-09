#pragma once
#include "assets_library/tools/landscape/landscape_pencil.h"

// Pencil for MaskAsset — same vertex-centric node flip as LandscapePencil;
// writes mask Landscape tiles to the MaskLandscape layer (the target layer
// is resolved from the asset's layerType).
class MaskPencil: public LandscapePencil
{
public:
    MaskPencil(QObject* parent);
};
