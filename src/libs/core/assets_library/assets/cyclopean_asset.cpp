#include "cyclopean_asset.h"

CyclopeanAsset::CyclopeanAsset(QObject* parent):
    SliceAsset(AssetTypes::cyclopean3d, parent)
{

}

void CyclopeanAsset::ensureCyclopeanData()
{
    if (!data.cyclopean3dData)
    {
        data.cyclopean3dData = BaseData::Cyclopean3dAssetData{};
    }
}

void CyclopeanAsset::load(const BaseData::AssetData& data_)
{
    Asset::load(data_);

    ensureCyclopeanData();

    // Optional preview thumbnail; there is no atlas to split (an empty atlas
    // path keeps the tiles list empty and thumbnail() falls back to the
    // explicit thumbnail image).
    if (!data.cyclopean3dData->thumbnail.empty())
    {
        loadAtlasFiles(data.root() / data.cyclopean3dData->thumbnail, {});
    }
}

QVariantMap CyclopeanAsset::cyclopeanParams() const
{
    QVariantMap out;
    if (!data.cyclopean3dData)
    {
        return out;
    }

    const BaseData::Cyclopean3dAssetData& d = *data.cyclopean3dData;
    out["raisedHeight"] = d.raisedHeight;
    out["rockSeed"] = d.rockSeed;
    out["rockAmplitude"] = d.rockAmplitude;
    out["rockEnabled"] = d.rockEnabled;
    out["cornerBevel"] = d.cornerBevel;
    out["wallSubdivH"] = d.wallSubdivH;
    out["wallSubdivV"] = d.wallSubdivV;
    return out;
}

void CyclopeanAsset::setCyclopeanParam(const QString& name, const QVariant& value)
{
    ensureCyclopeanData();
    BaseData::Cyclopean3dAssetData& d = *data.cyclopean3dData;

    if (name == "raisedHeight") d.raisedHeight = value.toFloat();
    else if (name == "rockSeed") d.rockSeed = value.toInt();
    else if (name == "rockAmplitude") d.rockAmplitude = value.toFloat();
    else if (name == "rockEnabled") d.rockEnabled = value.toBool();
    else if (name == "cornerBevel") d.cornerBevel = value.toFloat();
    else if (name == "wallSubdivH") d.wallSubdivH = value.toInt();
    else if (name == "wallSubdivV") d.wallSubdivV = value.toInt();
}
