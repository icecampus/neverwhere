#include "assets_loader.h"
#include <fstream>
#include <magic_enum/magic_enum.hpp>

#include "assets/image_asset.h"
#include "assets/slice_asset.h"
#include "assets_pack_model.h"

namespace fs = std::filesystem;

void AssetsLoader::load(const std::filesystem::path& rootPath, AssetsLibraryModel& library)
{
    if (!fs::exists(rootPath))
    {
        std::cerr << "The root directory does not exist: " << rootPath << std::endl;
        return;
    }

    for (const auto& entry : fs::directory_iterator(rootPath)) 
    {
        if (!entry.is_directory()) 
        {
            continue;
        }
        fs::path packPath = entry.path();

        library.addElement<AssetsPackModel>(packPath, &library);
        AssetsPackModel* lastPack = library.element(library.size()-1);
        loadPack(packPath, lastPack);
    }
}

void AssetsLoader::loadPack(const std::filesystem::path& packPath, AssetsPackModel* model)
{
    const std::string& groupName = packPath.filename().string();
    
    if (!fs::exists(packPath) || !fs::is_directory(packPath)) 
    {
        std::cerr << "Group directory does not exist: " << packPath << std::endl;
        return;
    }

    for (const auto& entry : fs::directory_iterator(packPath)) 
    {
        if (entry.is_directory()) 
        {
            loadAsset(entry.path(), model);
        }
    }
}

void AssetsLoader::loadAsset(const fs::path& assetPath, AssetsPackModel* pack)
{
    fs::path jsonFilePath = assetPath / "index.json";

    if (!fs::exists(jsonFilePath)) 
    {
        std::cerr << "JSON file not found: " << jsonFilePath << std::endl;
        return;
    }

    std::ifstream jsonFile(jsonFilePath);
    if (!jsonFile.is_open()) 
    {
        std::cerr << "Failed to open JSON file: " << jsonFilePath << std::endl;
        return;
    }

    json j;
    jsonFile >> j;

    if ( j.count(magic_enum::enum_name(AssetTypes::image)) )
    {
        auto asset = std::make_unique<ImageAsset>(pack);
        asset->load(jsonFilePath, j);
        pack->addAsset(std::move(asset));
    }

    if (j.count(magic_enum::enum_name(AssetTypes::slice)))
    {
        auto asset = std::make_unique<SliceAsset>(pack);
        asset->load(jsonFilePath, j);
        pack->addAsset(std::move(asset));
    }

}


//void AssetLoader::saveGroup(const std::string& groupName, const AssetPack* model) 
//{
//    fs::path groupPath = m_rootPath / groupName;
//
//    if (!fs::exists(groupPath)) 
//    {
//        if (!fs::create_directories(groupPath)) 
//        {
//            std::cerr << "Failed to create group directory: " << groupPath << std::endl;
//            return;
//        }
//    }
//
//    for (int i = 0; i < model->rowCount(); ++i) 
//    {
//        QModelIndex index = model->index(i);
//        ImageAsset* asset = model->data(index, AssetPack::ElementRole).value<ImageAsset*>();
//        if (asset) 
//        {
//            saveAsset(groupPath, asset);
//        }
//    }
//}
//
//
//
////void AssetLoader::saveAsset(const fs::path& groupPath, ImageAsset* asset)
////{
////
////}
