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
        Cloud,
        Fence
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
        CyclopeanLandscape,
        StoneLandscape,
        TextureLandscape,
        TechLandscape,
        MaskLandscape,
        FenceLandscape
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
        int footprintWidth{1};
        int footprintHeight{1};

        NLOHMANN_DEFINE_TYPE_INTRUSIVE_WITH_DEFAULT(BuildingData, footprintWidth, footprintHeight);
    };

    // One fence piece (fence3d asset, FenceLandscape layer): a post (kind 0,
    // 1 cell) or a section (kind 1, `length` cells along (axisX,axisY)).
    // Endpoint links and fence components are derived state (fence_core).
    struct FenceData
    {
        int kind = 0;
        int axisX = 0;
        int axisY = 0;
        int length = 1;
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
        std::optional<FenceData> fenceData;
    };

    void to_json(nlohmann::json& j, const Tile2DData&);
    void from_json(const nlohmann::json&, Tile2DData&);

    void to_json(nlohmann::json& j, const ResourceData&);
    void from_json(const nlohmann::json&, ResourceData&);

    void to_json(nlohmann::json& j, const LandscapeData& data);
    void from_json(const nlohmann::json& j, LandscapeData& data);

    void to_json(nlohmann::json& j, const FenceData& data);
    void from_json(const nlohmann::json& j, FenceData& data);

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