#pragma once
#include "assets_library/assets/slice_asset.h"

// Texture2D asset (tiling-texture landscape brush): no atlas — the payload is
// the "texture2d" block in index.json (tiling texture file + repeats per
// cell). Lives on the TextureLandscape layer; its Landscape tiles encode the
// vertex nodes (same tileIndex convention as slice/raised, so the
// pencil/RPC/authoring flow works unchanged).
//
// v1 has no settings panel: the payload is read-only through textureData()
// and reaches the renderer via the canonical AssetData on the next sync.
class TextureAsset: public SliceAsset
{
    Q_OBJECT

public:
    explicit TextureAsset(QObject* parent);

    void load(const BaseData::AssetData& data) override;

    const BaseData::Texture2dAssetData* textureData() const
    {
        return data.texture2dData ? &*data.texture2dData : nullptr;
    }
};
