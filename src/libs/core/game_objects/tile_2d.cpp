#include "tile_2d.h"
#include "base_data/lib.h"

Tile2D::Tile2D(QObject* parent ):
    GameObject(GameObjectTypes::Tile2D, parent)
{
    data.tile2dData = std::make_optional<BaseData::Tile2DData>();
}

void Tile2D::load(const BaseData::GameObject& data)
{
    GameObject::load(data);

    
}

