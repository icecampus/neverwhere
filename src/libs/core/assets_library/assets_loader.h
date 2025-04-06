#pragma once
#include <filesystem>
#include "assets_library.h"


struct AssetsLoader 
{
public:
    static void load(const std::filesystem::path& rootPath, AssetsLibrary& library);

    static void loadPack(const std::filesystem::path& packPath, AssetPack* model);
    static void saveGroup(const std::filesystem::path& rootPath, const std::string& groupName, const AssetPack* model);

    static void loadAsset(const std::filesystem::path& assetPath, AssetPack* model);
    
    //void saveAsset(const fs::path& groupPath, ImageAsset* asset) ;

    
};