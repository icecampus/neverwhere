#pragma once
#include "map_model.h"
#include "game_objects/land_tile.h"
#include "game_objects/resource.h"
#include "game_objects/building.h"

class MapLoader
{
public:
    MapLoader() : m_model(std::make_unique<MapModel>()) {}
    virtual ~MapLoader() = default;

    virtual void loadMap() {

        auto land = std::make_unique<LandTile>();
        land->setName("Grass");
        land->setPassable(true);
        m_model->addGameObject(std::move(land));

        auto resource = std::make_unique<Resource>();
        resource->setName("Wood");
        resource->setQuantity(100);
        m_model->addGameObject(std::move(resource));

        auto building = std::make_unique<Building>();
        building->setName("House");
        building->setLevel(2);
        m_model->addGameObject(std::move(building));
    }

    MapModel* model() const { return m_model.get(); }

private:
    std::unique_ptr<MapModel> m_model;
};