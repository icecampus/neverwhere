#include "building.h"


Building::Building(QObject* parent):
    GameObject(GameObjectTypes::Buildings, parent), 
    m_level(1) 
{

}

void Building::load(const BaseData::GameObject& data)
{
    GameObject::load(data);
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
