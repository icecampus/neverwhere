#include "building3d_asset.h"

Building3dAsset::Building3dAsset(QObject* parent):
    SliceAsset(AssetTypes::building3d, parent)
{
}

void Building3dAsset::ensureBuildingData()
{
    if (!data.building3dData)
    {
        data.building3dData = BaseData::Building3dAssetData{};
    }
}

void Building3dAsset::load(const BaseData::AssetData& data_)
{
    Asset::load(data_);

    ensureBuildingData();

    if (!data.building3dData->thumbnail.empty())
    {
        loadAtlasFiles(data.root() / data.building3dData->thumbnail, {});
    }
}

int Building3dAsset::footprintWidth() const
{
    return data.building3dData ? data.building3dData->footprintWidth : 3;
}

int Building3dAsset::footprintHeight() const
{
    return data.building3dData ? data.building3dData->footprintHeight : 3;
}

QVariantMap Building3dAsset::buildingParams() const
{
    QVariantMap out;
    if (!data.building3dData)
    {
        return out;
    }

    const BaseData::Building3dAssetData& d = *data.building3dData;
    out["model"] = QString::fromStdString(d.model);
    out["albedo"] = QString::fromStdString(d.albedo);
    out["thumbnail"] = QString::fromStdString(d.thumbnail);
    out["footprintWidth"] = d.footprintWidth;
    out["footprintHeight"] = d.footprintHeight;
    out["heightScale"] = d.heightScale;
    out["yawDegrees"] = d.yawDegrees;
    out["scale"] = d.scale;
    return out;
}

void Building3dAsset::setBuildingParam(const QString& name, const QVariant& value)
{
    ensureBuildingData();
    BaseData::Building3dAssetData& d = *data.building3dData;

    if (name == "model") d.model = value.toString().toStdString();
    else if (name == "albedo") d.albedo = value.toString().toStdString();
    else if (name == "thumbnail") d.thumbnail = value.toString().toStdString();
    else if (name == "footprintWidth")
    {
        d.footprintWidth = qMax(1, value.toInt());
        emit footprintChanged();
    }
    else if (name == "footprintHeight")
    {
        d.footprintHeight = qMax(1, value.toInt());
        emit footprintChanged();
    }
    else if (name == "heightScale") d.heightScale = value.toFloat();
    else if (name == "yawDegrees") d.yawDegrees = value.toFloat();
    else if (name == "scale") d.scale = value.toFloat();
}
