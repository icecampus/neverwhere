#pragma once
#include <QObject>
#include "boost_uuids_serialization.h"
#include <filesystem>
#include "maps.h"
#include "math/lib.h"

namespace AssetTypes
{
    Q_NAMESPACE;
    enum Type
    {
        image,
        slice
    };
    Q_ENUM_NS(Type);
}

namespace BaseData
{
    struct ImageAssetData
    {
        float width{0};
        std::string imageFilename;

        NLOHMANN_DEFINE_TYPE_INTRUSIVE(ImageAssetData, imageFilename, width);
    };

    struct SliceAssetData
    {
     
        std::string thumbnail;
        std::string atlas;
        NLOHMANN_DEFINE_TYPE_INTRUSIVE(SliceAssetData, thumbnail, atlas);
    };

    //Asset
    struct AssetData
    {
        std::filesystem::path indexPath;
        boost::uuids::uuid uuid;
        std::string name;
        LayerTypes::Type layerType{ LayerTypes::Decoration };
        math::vec2 pivot{ 0.5, -1.0f };

        std::optional<ImageAssetData> imageData;
        std::optional<SliceAssetData> sliceData;

        static AssetData load(const std::filesystem::path& assetsPath);
        static void save(const AssetData& assetData, const std::filesystem::path& assetsPath);

        std::filesystem::path root() const;
    };

    void to_json(nlohmann::json& j, const AssetData& obj);
    void from_json(const nlohmann::json& j, AssetData& obj);

    //AsssetsPack
    struct AssetsPack: public std::deque<AssetData> 
    {
        static AssetsPack load(const std::filesystem::path& assetsPath);
        static void save(const AssetsPack& assetsPack, const std::filesystem::path& assetsPath);

        std::filesystem::path path;
   
    };

    //Assets
    struct Assets: public std::deque<AssetsPack>
    {
        static Assets load(const std::filesystem::path& assetsPath);
        static void save(const Assets& assets, const std::filesystem::path& assetsPath);
    };

}