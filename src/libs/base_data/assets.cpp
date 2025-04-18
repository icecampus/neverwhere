#include "assets.h"
#include <fstream>
#include <magic_enum/magic_enum.hpp>


namespace fs = std::filesystem;

namespace BaseData
{

void to_json(nlohmann::json& j, const AssetData& obj)
{
    j = nlohmann::json
    {
        {"uuid", obj.uuid},
        {"layerType", obj.layerType},
        {"pivot", obj.pivot}
    };

    if (obj.imageData)
    {
        j["image"] = *obj.imageData;
    }
    if (obj.sliceData)
    {
        j["slice"] = *obj.sliceData;
    }
}

void from_json(const nlohmann::json& j, AssetData& obj)
{
    j.at("uuid").get_to(obj.uuid);
    
    std::string layaerSrt = j["layerType"];
    auto layreType = magic_enum::enum_cast<LayerTypes::Type>(layaerSrt);
    if (layreType.has_value())
    {
        obj.layerType = layreType.value();
    }
    else
    {
        spdlog::error("Failed read layer type: {} of asset at: {}", layaerSrt, boost::uuids::to_string(obj.uuid));
    }
    
    obj.pivot = {0,0};

    //j.at("pivot").get_to(obj.pivot);

    if (j.contains("image"))
    {
        obj.imageData = j["image"].get<ImageAssetData>();
    }
    if (j.contains("slice"))
    {
        obj.sliceData = j["slice"].get<SliceAssetData>();
    }
}

AssetData AssetData::load(const std::filesystem::path& path)
{
    fs::path indexPath = path / "index.json";

    std::ifstream file(indexPath);
    if (!file.is_open())
    {
        spdlog::error("Failed to open file: {}",  path.string());
    }

    nlohmann::json j;
    file >> j;

    AssetData asset = j;
    asset.indexPath = indexPath;
    asset.name = path.filename().string();
    return asset;


}

void AssetData::save(const AssetData& assetData, const std::filesystem::path& path)
{
    nlohmann::json j;
    j = assetData;

    std::ofstream file(path);
    if (!file.is_open())
    {
        spdlog::error("Failed to create file: " + path.string());
    }

    file << j.dump(4);
}


std::filesystem::path AssetData::root() const
{
    return indexPath.parent_path();
}

//AssetsPack
AssetsPack AssetsPack::load(const std::filesystem::path& path)
{
    if (!fs::exists(path))
    {
        spdlog::error("The asset library directory does not exist: {}", path.string());
        return {};
    }

    AssetsPack pack;
    pack.path = path;

    for (const auto& entry : fs::directory_iterator(path))
    {
        if (entry.is_directory())
        {
            AssetData asset = AssetData::load(entry);
            pack.push_back(asset);
        }
    }

    return pack;
}

void AssetsPack::save(const AssetsPack& assetsPack, const std::filesystem::path& assetsPath)
{

}

//Assets
Assets Assets::load(const std::filesystem::path& assetsPath)
{
    if (!fs::exists(assetsPath))
    {
        spdlog::error("The asset library directory does not exist: {}", assetsPath.string() );
        return {};
    }

    Assets assets;
    for (const auto& entry : fs::directory_iterator(assetsPath))
    {
        if (!entry.is_directory())
        {
            continue;
        }
        fs::path packPath = entry.path();

        AssetsPack pack = AssetsPack::load(packPath);
        assets.push_back(pack);
    }
    return assets;
}

void Assets::save(const Assets& assets, const std::filesystem::path& assetsPath)
{

}

}//