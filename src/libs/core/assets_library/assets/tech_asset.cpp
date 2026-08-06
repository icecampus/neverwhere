#include "tech_asset.h"

namespace {

void putColor(QVariantMap& out, const QString& prefix, const std::array<float, 3>& c) {
    for (int i = 0; i < 3; ++i) {
        out[QString("%1.%2").arg(prefix).arg(i)] = c[static_cast<size_t>(i)];
    }
}

} // namespace

TechAsset::TechAsset(QObject* parent):
    SliceAsset(AssetTypes::tech3d, parent)
{

}

void TechAsset::ensureTechData()
{
    if (!data.tech3dData)
    {
        data.tech3dData = BaseData::Tech3dAssetData{};
    }
}

void TechAsset::load(const BaseData::AssetData& data_)
{
    Asset::load(data_);

    ensureTechData();

    // Optional palette preview thumbnail; there is no atlas to split (an
    // empty atlas path keeps the tiles list empty and thumbnail() falls back
    // to the explicit thumbnail image).
    if (!data.tech3dData->thumbnail.empty())
    {
        loadAtlasFiles(data.root() / data.tech3dData->thumbnail, {});
    }
}

QVariantMap TechAsset::techParams() const
{
    QVariantMap out;
    if (!data.tech3dData)
    {
        return out;
    }

    const BaseData::Tech3dAssetData& d = *data.tech3dData;
    out["raisedHeight"] = d.raisedHeight;
    out["cellSize"] = d.cellSize;
    out["padding"] = d.padding;
    out["levelHeight"] = d.levelHeight;
    out["groundDepth"] = d.groundDepth;
    out["style"] = d.style;
    out["soften"] = d.soften;
    out["creaseWidth"] = d.creaseWidth;
    out["blurPasses"] = d.blurPasses;
    out["outlineDepth"] = d.outlineDepth;

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
    return out;
}

void TechAsset::setTechParam(const QString& name, const QVariant& value)
{
    ensureTechData();
    BaseData::Tech3dAssetData& d = *data.tech3dData;
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
    else if (name == "levelHeight") d.levelHeight = value.toFloat();
    else if (name == "groundDepth") d.groundDepth = value.toFloat();
    else if (name == "style") d.style = value.toFloat();
    else if (name == "soften") d.soften = value.toFloat();
    else if (name == "creaseWidth") d.creaseWidth = value.toFloat();
    else if (name == "blurPasses") d.blurPasses = value.toInt();
    else if (name == "outlineDepth") d.outlineDepth = value.toFloat();
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
}
