#include "shape3d_asset.h"

Shape3dAsset::Shape3dAsset(QObject* parent):
    SliceAsset(AssetTypes::shape3d, parent)
{

}

void Shape3dAsset::load(const BaseData::AssetData& data)
{
    Asset::load(data);

    if (!data.shape3dData)
    {
        return;
    }

    loadAtlasFiles(data.root() / data.shape3dData->thumbnail, data.root() / data.shape3dData->atlas);

    m_raisedHeight = data.shape3dData->raisedHeight;
    m_rockWalls = data.shape3dData->rockWalls;
    m_rockAmplitude = data.shape3dData->rockAmplitude;
    m_rockBevel = data.shape3dData->rockBevel;
}

float Shape3dAsset::raisedHeight() const
{
    return m_raisedHeight;
}

bool Shape3dAsset::rockWalls() const
{
    return m_rockWalls;
}

float Shape3dAsset::rockAmplitude() const
{
    return m_rockAmplitude;
}

float Shape3dAsset::rockBevel() const
{
    return m_rockBevel;
}
