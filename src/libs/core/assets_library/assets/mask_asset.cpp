#include "mask_asset.h"

namespace {

void putColor(QVariantMap& out, const QString& prefix, const std::array<float, 3>& c) {
    for (int i = 0; i < 3; ++i) {
        out[QString("%1.%2").arg(prefix).arg(i)] = c[static_cast<size_t>(i)];
    }
}

} // namespace

MaskAsset::MaskAsset(QObject* parent):
    SliceAsset(AssetTypes::mask3d, parent)
{

}

void MaskAsset::ensureMaskData()
{
    if (!data.mask3dData)
    {
        data.mask3dData = BaseData::Mask3dAssetData{};
    }
}

void MaskAsset::load(const BaseData::AssetData& data_)
{
    Asset::load(data_);

    ensureMaskData();

    // Optional palette preview thumbnail; there is no atlas to split (an
    // empty atlas path keeps the tiles list empty and thumbnail() falls back
    // to the explicit thumbnail image).
    if (!data.mask3dData->thumbnail.empty())
    {
        loadAtlasFiles(data.root() / data.mask3dData->thumbnail, {});
    }
}

QVariantMap MaskAsset::maskParams() const
{
    QVariantMap out;
    if (!data.mask3dData)
    {
        return out;
    }

    const BaseData::Mask3dAssetData& d = *data.mask3dData;
    out["raisedHeight"] = d.raisedHeight;
    out["cellSize"] = d.cellSize;
    out["padding"] = d.padding;
    out["height"] = d.height;
    out["spreadDistance"] = d.spreadDistance;
    out["sinkFraction"] = d.sinkFraction;
    out["blurPasses"] = d.blurPasses;
    out["reliefDepth"] = d.reliefDepth;
    out["reliefTiling"] = d.reliefTiling;
    out["reliefFade"] = d.reliefFade;
    out["materialSet"] = QString::fromStdString(d.materialSet);
    out["matTiling"] = d.matTiling;
    out["matAlbedo"] = d.matAlbedo;
    out["matNormal"] = d.matNormal;
    out["matAo"] = d.matAo;
    out["matRough"] = d.matRough;

    const BaseData::Cliff3dShadingData& s = d.shading;
    out["shading.lightAzimuth"] = s.lightAzimuth;
    out["shading.lightElevation"] = s.lightElevation;
    putColor(out, "shading.darkColor", s.darkColor);
    putColor(out, "shading.goldColor", s.goldColor);
    putColor(out, "shading.grassA", s.grassA);
    putColor(out, "shading.grassB", s.grassB);
    out["shading.veinThreshold"] = s.veinThreshold;
    out["shading.ambient"] = s.ambient;
    out["shading.diffuse"] = s.diffuse;
    out["shading.backLight"] = s.backLight;
    out["shading.specStrength"] = s.specStrength;
    out["shading.specPower"] = s.specPower;
    out["shading.gamma"] = s.gamma;
    out["shading.texScale"] = s.texScale;
    out["shading.bottomDarken"] = s.bottomDarken;
    out["shading.bottomBand"] = s.bottomBand;
    out["shading.strataStrength"] = s.strataStrength;
    out["shading.underwaterFade"] = s.underwaterFade;
    return out;
}

void MaskAsset::setMaskParam(const QString& name, const QVariant& value)
{
    ensureMaskData();
    BaseData::Mask3dAssetData& d = *data.mask3dData;
    BaseData::Cliff3dShadingData& s = d.shading;

    const auto setChannel = [&value](std::array<float, 3>& c, const QString& channel) {
        bool ok = false;
        const int i = channel.toInt(&ok);
        if (ok && i >= 0 && i < 3) {
            c[static_cast<size_t>(i)] = value.toFloat();
        }
    };

    // Indexed keys first (colors), then scalars.
    if (name.startsWith("shading.darkColor.")) setChannel(s.darkColor, name.mid(18));
    else if (name.startsWith("shading.goldColor.")) setChannel(s.goldColor, name.mid(18));
    else if (name.startsWith("shading.grassA.")) setChannel(s.grassA, name.mid(15));
    else if (name.startsWith("shading.grassB.")) setChannel(s.grassB, name.mid(15));
    else if (name == "raisedHeight") d.raisedHeight = value.toFloat();
    else if (name == "cellSize") d.cellSize = value.toFloat();
    else if (name == "padding") d.padding = value.toFloat();
    else if (name == "height") d.height = value.toFloat();
    else if (name == "spreadDistance") d.spreadDistance = value.toFloat();
    else if (name == "sinkFraction") d.sinkFraction = value.toFloat();
    else if (name == "blurPasses") d.blurPasses = value.toInt();
    else if (name == "reliefDepth") d.reliefDepth = value.toFloat();
    else if (name == "reliefTiling") d.reliefTiling = value.toFloat();
    else if (name == "reliefFade") d.reliefFade = value.toFloat();
    else if (name == "materialSet") d.materialSet = value.toString().toStdString();
    else if (name == "matTiling") d.matTiling = value.toFloat();
    else if (name == "matAlbedo") d.matAlbedo = value.toFloat();
    else if (name == "matNormal") d.matNormal = value.toFloat();
    else if (name == "matAo") d.matAo = value.toFloat();
    else if (name == "matRough") d.matRough = value.toFloat();
    else if (name == "shading.lightAzimuth") s.lightAzimuth = value.toFloat();
    else if (name == "shading.lightElevation") s.lightElevation = value.toFloat();
    else if (name == "shading.veinThreshold") s.veinThreshold = value.toFloat();
    else if (name == "shading.ambient") s.ambient = value.toFloat();
    else if (name == "shading.diffuse") s.diffuse = value.toFloat();
    else if (name == "shading.backLight") s.backLight = value.toFloat();
    else if (name == "shading.specStrength") s.specStrength = value.toFloat();
    else if (name == "shading.specPower") s.specPower = value.toFloat();
    else if (name == "shading.gamma") s.gamma = value.toFloat();
    else if (name == "shading.texScale") s.texScale = value.toFloat();
    else if (name == "shading.bottomDarken") s.bottomDarken = value.toFloat();
    else if (name == "shading.bottomBand") s.bottomBand = value.toFloat();
    else if (name == "shading.strataStrength") s.strataStrength = value.toFloat();
    else if (name == "shading.underwaterFade") s.underwaterFade = value.toFloat();
}
