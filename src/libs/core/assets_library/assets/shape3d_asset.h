#pragma once
#include "assets_library/assets/slice_asset.h"

// Shape3D asset (TileShapePlayground port): a slice atlas whose Landscape
// tiles are rendered raised (offset top + cliff walls). Lives on the
// RaisedLandscape layer; presentation params come from the "shape3d"
// payload in index.json.
class Shape3dAsset: public SliceAsset
{
    Q_OBJECT

public:
    explicit Shape3dAsset(QObject* parent);

    void load(const BaseData::AssetData& data) override;

    float raisedHeight() const;
    bool rockWalls() const;
    float rockAmplitude() const;
    float rockBevel() const;

private:
    float m_raisedHeight = 32.0f;
    bool m_rockWalls = true;
    float m_rockAmplitude = 0.28f;
    float m_rockBevel = 0.3f;
};
