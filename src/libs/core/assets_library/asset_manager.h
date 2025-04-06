#pragma once
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "asset.h"       
#include "asset_pack.h" 
#include "assets/image_asset.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

class AssetManager 
{
public:
    explicit AssetManager(const fs::path& rootPath);

    void loadGroup(const std::string& groupName, AssetPack* model);
    void saveGroup(const std::string& groupName, const AssetPack* model);

private:
    void loadAsset(const fs::path& assetPath, AssetPack* model);
    void saveAsset(const fs::path& groupPath, ImageAsset* asset) ;

    fs::path m_rootPath; 
};