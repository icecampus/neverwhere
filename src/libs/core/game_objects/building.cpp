#include "building.h"


Building::Building(QObject* parent):
    GameObject(GameObjectTypes::Buildings, parent), 
    m_level(1) 
{

}

int Building::level() const 
{
    return m_level;
}

void Building::setLevel(int level) 
{
    if (m_level != level) 
    {
        m_level = level;
        emit levelChanged();
    }
}
