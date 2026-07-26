#include "cliff_asset.h"

namespace {

void putColor(QVariantMap& out, const QString& prefix, const std::array<float, 3>& c) {
    for (int i = 0; i < 3; ++i) {
        out[QString("%1.%2").arg(prefix).arg(i)] = c[static_cast<size_t>(i)];
    }
}

} // namespace

CliffAsset::CliffAsset(QObject* parent):
    SliceAsset(AssetTypes::cliff3d, parent)
{

}

void CliffAsset::ensureCliffData()
{
    if (!data.cliff3dData)
    {
        data.cliff3dData = BaseData::Cliff3dAssetData{};
    }
}

void CliffAsset::load(const BaseData::AssetData& data_)
{
    Asset::load(data_);

    ensureCliffData();

    // Optional palette preview thumbnail; there is no atlas to split (an
    // empty atlas path keeps the tiles list empty and thumbnail() falls back
    // to the explicit thumbnail image).
    if (!data.cliff3dData->thumbnail.empty())
    {
        loadAtlasFiles(data.root() / data.cliff3dData->thumbnail, {});
    }
}

QVariantMap CliffAsset::cliffParams() const
{
    QVariantMap out;
    if (!data.cliff3dData)
    {
        return out;
    }

    const BaseData::Cliff3dAssetData& d = *data.cliff3dData;
    out["raisedHeight"] = d.raisedHeight;
    out["topTexture"] = QString::fromStdString(d.topTexture);
    out["flareAmount"] = d.flareAmount;
    out["flareBand"] = d.flareBand;
    out["cellSize"] = d.cellSize;
    out["padding"] = d.padding;
    out["plateauHeight"] = d.plateauHeight;
    out["d2Scale"] = d.d2Scale;
    out["blurRadiusCells"] = d.blurRadiusCells;
    out["blurPasses"] = d.blurPasses;
    out["edgeRadius"] = d.edgeRadius;
    out["grooveMaskWidth"] = d.grooveMaskWidth;
    out["grooveFadeK"] = d.grooveFadeK;
    out["grooveRimFade"] = d.grooveRimFade;
    out["fbmAmplitude"] = d.fbmAmplitude;
    out["fbmFrequency"] = d.fbmFrequency;
    out["fbmOctaves"] = d.fbmOctaves;
    out["groundDepth"] = d.groundDepth;
    out["groundMargin"] = d.groundMargin;
    out["groundRounding"] = d.groundRounding;
    out["groundEnabled"] = d.groundEnabled;
    out["groovePeriod"] = d.groovePeriod;
    out["groovePhase"] = d.groovePhase;
    out["grooveDepthMax"] = d.grooveDepthMax;
    out["grooveSmooth"] = d.grooveSmooth;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 2; ++j) {
            out[QString("grooveAngles.%1.%2").arg(i).arg(j)] = d.grooveAngles[static_cast<size_t>(i)][static_cast<size_t>(j)];
        }
    }

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

void CliffAsset::setCliffParam(const QString& name, const QVariant& value)
{
    ensureCliffData();
    BaseData::Cliff3dAssetData& d = *data.cliff3dData;
    BaseData::Cliff3dShadingData& s = d.shading;

    const auto setChannel = [&value](std::array<float, 3>& c, const QString& channel) {
        bool ok = false;
        const int i = channel.toInt(&ok);
        if (ok && i >= 0 && i < 3) {
            c[static_cast<size_t>(i)] = value.toFloat();
        }
    };
    const auto setAngle = [&value](std::array<float, 2>& a, const QString& idx) {
        bool ok = false;
        const int j = idx.toInt(&ok);
        if (ok && j >= 0 && j < 2) {
            a[static_cast<size_t>(j)] = value.toFloat();
        }
    };

    // Indexed keys first (colors, groove angles), then scalars.
    if (name.startsWith("shading.darkColor.")) setChannel(s.darkColor, name.mid(18));
    else if (name.startsWith("shading.goldColor.")) setChannel(s.goldColor, name.mid(18));
    else if (name.startsWith("shading.grassA.")) setChannel(s.grassA, name.mid(15));
    else if (name.startsWith("shading.grassB.")) setChannel(s.grassB, name.mid(15));
    else if (name.startsWith("grooveAngles.")) {
        const QStringList parts = name.split('.');
        if (parts.size() == 3) {
            bool ok = false;
            const int i = parts[1].toInt(&ok);
            if (ok && i >= 0 && i < 3) {
                setAngle(d.grooveAngles[static_cast<size_t>(i)], parts[2]);
            }
        }
    }
    else if (name == "raisedHeight") d.raisedHeight = value.toFloat();
    else if (name == "topTexture") d.topTexture = value.toString().toStdString();
    else if (name == "flareAmount") d.flareAmount = value.toFloat();
    else if (name == "flareBand") d.flareBand = value.toFloat();
    else if (name == "cellSize") d.cellSize = value.toFloat();
    else if (name == "padding") d.padding = value.toFloat();
    else if (name == "plateauHeight") d.plateauHeight = value.toFloat();
    else if (name == "d2Scale") d.d2Scale = value.toFloat();
    else if (name == "blurRadiusCells") d.blurRadiusCells = value.toInt();
    else if (name == "blurPasses") d.blurPasses = value.toInt();
    else if (name == "edgeRadius") d.edgeRadius = value.toFloat();
    else if (name == "grooveMaskWidth") d.grooveMaskWidth = value.toFloat();
    else if (name == "grooveFadeK") d.grooveFadeK = value.toFloat();
    else if (name == "grooveRimFade") d.grooveRimFade = value.toFloat();
    else if (name == "fbmAmplitude") d.fbmAmplitude = value.toFloat();
    else if (name == "fbmFrequency") d.fbmFrequency = value.toFloat();
    else if (name == "fbmOctaves") d.fbmOctaves = value.toInt();
    else if (name == "groundDepth") d.groundDepth = value.toFloat();
    else if (name == "groundMargin") d.groundMargin = value.toFloat();
    else if (name == "groundRounding") d.groundRounding = value.toFloat();
    else if (name == "groundEnabled") d.groundEnabled = value.toBool();
    else if (name == "groovePeriod") d.groovePeriod = value.toFloat();
    else if (name == "groovePhase") d.groovePhase = value.toFloat();
    else if (name == "grooveDepthMax") d.grooveDepthMax = value.toFloat();
    else if (name == "grooveSmooth") d.grooveSmooth = value.toFloat();
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
