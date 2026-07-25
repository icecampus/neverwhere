#pragma once
#include "assets_library/tools/landscape/landscape_pencil.h"

// Pencil for CliffAsset — same vertex-centric node flip as LandscapePencil;
// writes cliff Landscape tiles to the CliffLandscape layer (the target layer
// is resolved from the asset's layerType).
class CliffPencil: public LandscapePencil
{
public:
    CliffPencil(QObject* parent);
};
