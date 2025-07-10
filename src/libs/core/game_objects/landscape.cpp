#include "landscape.h"
#include "assets_library/assets/slice_asset.h"

Landscape::Landscape(QObject* parent ):
    GameObject(GameObjectTypes::Landscape, parent)
{
    data.landscapeData = std::make_optional<BaseData::LandscapeData>(BaseData::LandscapeData{TileSet::Unknown});
}

void Landscape::load(const BaseData::GameObject& data)
{
    GameObject::load(data);
    emit tileIndexChanged();
}

size_t Landscape::getTileIndex() const
{
    return data.landscapeData->tileIndex;
}

void Landscape::setTileIndex(size_t index)
{
    if (data.landscapeData->tileIndex != index)
    {
        data.landscapeData->tileIndex = index;
        emit tileIndexChanged();
    }
}

