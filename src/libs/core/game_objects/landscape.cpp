#include "landscape.h"

Landscape::Landscape(QObject* parent ):
    GameObject(GameObjectTypes::Landscape, parent)
{

}

size_t Landscape::getTileIndex() const
{
    return tileIndex;
}

void Landscape::setTileIndex(size_t index)
{
    if (tileIndex != index)
    {
        tileIndex = index;
        emit tileIndexChanged();
    }
}

