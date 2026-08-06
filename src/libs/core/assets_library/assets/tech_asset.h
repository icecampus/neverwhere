#pragma once
#include "assets_library/assets/slice_asset.h"

#include <QVariantMap>

// Tech3D asset (SDFGeneratedLandscape "Tech 3D" brush port): no atlas — the
// tech-field generator parameter set + shading palette from the "tech3d"
// payload in index.json. Lives on the TechLandscape layer; its Landscape
// tiles encode the vertex nodes (same tileIndex convention as slice/raised,
// so the pencil/RPC/authoring flow works unchanged).
//
// Params are edited live from the right-panel settings UI through
// techParams()/setTechParam(); they write the canonical AssetData payload,
// so the renderer (ensureFrameAssets) picks changes on the next sync and
// AssetsLibraryModel::save serializes them back to index.json.
class TechAsset: public SliceAsset
{
    Q_OBJECT

public:
    explicit TechAsset(QObject* parent);

    void load(const BaseData::AssetData& data) override;

    const BaseData::Tech3dAssetData& techData() const { return *data.tech3dData; }
    BaseData::Tech3dAssetData& techData() { return *data.tech3dData; }

    // Flat parameter map for the settings UI: plain scalar keys ("cellSize",
    // "style", ...), shading scalars ("shading.ambient") and color channels
    // ("shading.darkColor.0").
    Q_INVOKABLE QVariantMap techParams() const;
    Q_INVOKABLE void setTechParam(const QString& name, const QVariant& value);

private:
    void ensureTechData();
};
