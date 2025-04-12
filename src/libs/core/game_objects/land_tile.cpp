#include "land_tile.h"

LandTile::LandTile(QObject* parent ):
    GameObject(GameObjectTypes::Landscape, parent), 
    m_isPassable(true) 
{

}

bool LandTile::isPassable() const 
{
    return m_isPassable;
}

void LandTile::setPassable(bool passable) 
{
    if (m_isPassable != passable) 
    {
        m_isPassable = passable;
        emit passableChanged();
    }
}
