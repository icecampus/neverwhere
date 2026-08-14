#include "maps.h"
#include <fstream>
#include <magic_enum/magic_enum.hpp>
// #include <spdlog/spdlog.h>


namespace BaseData
{
    void to_json(nlohmann::json& j, const Tile2DData&) { j = {}; }
    void from_json(const nlohmann::json&, Tile2DData&) {}

    void to_json(nlohmann::json& j, const ResourceData&) { j = {}; }
    void from_json(const nlohmann::json&, ResourceData&) {}

    void to_json(nlohmann::json& j, const LandscapeData& data) 
    {
        j = nlohmann::json{ {"tileIndex", data.tileIndex} };
    }

    void from_json(const nlohmann::json& j, LandscapeData& data) 
    {
        j.at("tileIndex").get_to(data.tileIndex);
    }

    void to_json(nlohmann::json& j, const GameObject& obj) 
    {
        j = nlohmann::json
        {
            {"position", obj.position},
            {"assetUuid", obj.assetUuid},
            {"type", obj.type}
        };

        if (obj.tile2dData) 
        {
            j["tile2dData"] = *obj.tile2dData;
        }
        if (obj.resourceData) 
        {
            j["resourceData"] = *obj.resourceData;
        }
        if (obj.landscapeData) 
        {
            j["landscapeData"] = *obj.landscapeData;
        }
        if (obj.buildingData)
        {
            j["buildingData"] = *obj.buildingData;
        }
    }

    void from_json(const nlohmann::json& j, GameObject& obj) 
    {
        j.at("position").get_to(obj.position);
        j.at("assetUuid").get_to(obj.assetUuid);
        j.at("type").get_to(obj.type);

        if (j.contains("tile2dData"))
        {
            obj.tile2dData = j["tile2dData"].get<Tile2DData>();
        }
        if (j.contains("resourceData"))
        {
            obj.resourceData = j["resourceData"].get<ResourceData>();
        }

        if (j.contains("landscapeData"))
        {
            obj.landscapeData = j["landscapeData"].get<LandscapeData>();
        }
        if (j.contains("buildingData"))
        {
            obj.buildingData = j["buildingData"].get<BuildingData>();
        }
    }


Map Map::load(const std::filesystem::path& chaptersPath) 
{
    std::ifstream file(chaptersPath);
    if (!file.is_open()) 
    {
         // spdlog::error("Failed to open file: " + chaptersPath.string());
    }

    nlohmann::json j;
    file >> j;

    Map map;
    for (const LayerTypes::Type layerType : magic_enum::enum_values<LayerTypes::Type>()) 
    {
        auto layerName = magic_enum::enum_name(layerType);
        // Older map files may lack newer layer keys — treat them as empty
        // (entry must still exist: Map::save addresses layers via .at()).
        Layer layer;
        if (j.contains(layerName) && j[layerName].is_array())
        {
            layer = j[layerName].get<Layer>();
        }
        map[layerType] = layer;
    }

    return map;
}

void Map::save(const Map& map, const std::filesystem::path& chaptersPath) 
{
    nlohmann::json j;
    for (const LayerTypes::Type layerType : magic_enum::enum_values<LayerTypes::Type>())
    {
        auto layerName = magic_enum::enum_name(layerType);
        j[layerName] = map.at(layerType); 
    }

    std::ofstream file(chaptersPath);
    if (!file.is_open()) 
    {
         // spdlog::error("Failed to create file: " + chaptersPath.string());
    }

    file << j.dump(4); 
}

}//