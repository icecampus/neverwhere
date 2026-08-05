#pragma once
#include "assets_library/tools/landscape/landscape_pencil.h"

// Pencil for TextureAsset — same vertex-centric node flip as LandscapePencil;
// writes texture Landscape tiles to the TextureLandscape layer (the target
// layer is resolved from the asset's layerType).
class TexturePencil: public LandscapePencil
{
public:
    TexturePencil(QObject* parent);
};
