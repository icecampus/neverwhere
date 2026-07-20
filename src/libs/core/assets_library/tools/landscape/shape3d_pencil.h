#pragma once
#include "assets_library/tools/landscape/landscape_pencil.h"

// Pencil for Shape3dAsset — same vertex-centric node flip as
// LandscapePencil; writes raised Landscape tiles to the RaisedLandscape
// layer (the target layer is resolved from the asset's layerType).
class Shape3dPencil: public LandscapePencil
{
public:
    Shape3dPencil(QObject* parent);
};
