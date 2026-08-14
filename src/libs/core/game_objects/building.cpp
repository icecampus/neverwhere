#include "building.h"


Building::Building(QObject* parent):
    GameObject(GameObjectTypes::Buildings, parent), 
    m_level(1) 
{
    data.buildingData = BaseData::BuildingData{};
}

void Building::setFootprint(int width, int height)
{
    if (!data.buildingData)
    {
        data.buildingData = BaseData::BuildingData{};
    }
    data.buildingData->footprintWidth = width > 0 ? width : 1;
    data.buildingData->footprintHeight = height > 0 ? height : 1;
}

void Building::load(const BaseData::GameObject& data)
{
    GameObject::load(data);
    if (!this->data.buildingData)
    {
        this->data.buildingData = BaseData::BuildingData{};
    }
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
