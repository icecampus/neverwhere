#pragma once
#include <deque>
#include <filesystem>
#include <optional>

#include <QObject>

#include "boost_uuids_serialization.h"
#include "math/lib.h"


namespace GameObjectTypes
{
    Q_NAMESPACE;
    enum Type
    {
        Tile2D,
        Landscape,
        Resource,
        Buildings,
        Cloud
    };
    Q_ENUM_NS(Type);
}

namespace LayerTypes
{
    Q_NAMESPACE;
    enum Type
    {
        Decoration,
        BaseLandscape,
        GameplayInteractive,
        RaisedLandscape,
        CliffLandscape,
        CyclopeanLandscape
    };
    Q_ENUM_NS(Type);
}


namespace BaseData
{
    struct Tile2DData
    {
    
    };

    struct ResourceData
    {
    };

    struct LandscapeData
    {
        size_t tileIndex;
    };

    struct BuildingData
    {
    
    };


    //GameObject
    struct GameObject
    {
        math::ivec2 position;
        boost::uuids::uuid assetUuid;
        GameObjectTypes::Type type;

        std::optional<Tile2DData> tile2dData;
        std::optional<ResourceData> resourceData;
        std::optional<LandscapeData> landscapeData;
        std::optional<BuildingData> buildingData;
    };

    void to_json(nlohmann::json& j, const Tile2DData&);
    void from_json(const nlohmann::json&, Tile2DData&);

    void to_json(nlohmann::json& j, const ResourceData&);
    void from_json(const nlohmann::json&, ResourceData&);

    void to_json(nlohmann::json& j, const BuildingData&);
    void from_json(const nlohmann::json&, BuildingData&);

    void to_json(nlohmann::json& j, const LandscapeData& data);
    void from_json(const nlohmann::json& j, LandscapeData& data);

    void to_json(nlohmann::json& j, const GameObject& obj);
    void from_json(const nlohmann::json& j, GameObject& obj);


    //
    struct Layer : public std::deque<GameObject>
    {

    };

    struct Map: public std::map<LayerTypes::Type, Layer>
    {
        static Map load(const std::filesystem::path& chaptersPath);
        static void save(const Map& map, const std::filesystem::path& chaptersPath);
   
    };

}