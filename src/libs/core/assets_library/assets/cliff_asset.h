#pragma once
#include "assets_library/assets/slice_asset.h"

// Cliff3D asset (TileShapePlayground "Cliff 3D" brush port): no atlas — the
// whole cliff-field generator parameter set + shading palette from the
// "cliff3d" payload in index.json. Lives on the CliffLandscape layer; its
// Landscape tiles encode the vertex nodes (same tileIndex convention as
// slice/raised, so the pencil/RPC/authoring flow works unchanged).
class CliffAsset: public SliceAsset
{
    Q_OBJECT

public:
    explicit CliffAsset(QObject* parent);

    void load(const BaseData::AssetData& data) override;

    const BaseData::Cliff3dAssetData& cliffData() const { return m_cliffData; }
    BaseData::Cliff3dAssetData& cliffData() { return m_cliffData; }

private:
    BaseData::Cliff3dAssetData m_cliffData;
};
