#include "asset_manager.h"

AssetManager::AssetManager(const fs::path& rootPath): 
    m_rootPath(rootPath)
{
    if (!fs::exists(m_rootPath)) 
    {
        std::cerr << "The root directory does not exist: " << m_rootPath << std::endl;
    }
}

void AssetManager::loadGroup(const std::string& groupName, AssetPack* model) 
{
    fs::path groupPath = m_rootPath / groupName;

    if (!fs::exists(groupPath)) 
    {
        std::cerr << "Group directory does not exist: " << groupPath << std::endl;
        return;
    }

    for (const auto& entry : fs::directory_iterator(groupPath)) 
    {
        if (entry.is_directory()) 
        {
            loadAsset(entry.path(), model);
        }
    }
}

void AssetManager::saveGroup(const std::string& groupName, const AssetPack* model) 
{
    fs::path groupPath = m_rootPath / groupName;

    if (!fs::exists(groupPath)) 
    {
        if (!fs::create_directories(groupPath)) 
        {
            std::cerr << "Failed to create group directory: " << groupPath << std::endl;
            return;
        }
    }

    for (int i = 0; i < model->rowCount(); ++i) 
    {
        QModelIndex index = model->index(i);
        ImageAsset* asset = model->data(index, AssetPack::ElementRole).value<ImageAsset*>();
        if (asset) 
        {
            saveAsset(groupPath, asset);
        }
    }
}


void AssetManager::loadAsset(const fs::path& assetPath, AssetPack* model)
{
    fs::path jsonFilePath = assetPath / "index.json";

    if (!fs::exists(jsonFilePath)) {
        std::cerr << "JSON file not found: " << jsonFilePath << std::endl;
        return;
    }

    std::ifstream jsonFile(jsonFilePath);
    if (!jsonFile.is_open()) {
        std::cerr << "Failed to open JSON file: " << jsonFilePath << std::endl;
        return;
    }

    json j;
    jsonFile >> j;
    auto asset = std::make_unique<ImageAsset>(model);

    asset->load(jsonFilePath, j);

    model->addAsset(std::move(asset));
}

void AssetManager::saveAsset(const fs::path& groupPath, ImageAsset* asset)
{

}
