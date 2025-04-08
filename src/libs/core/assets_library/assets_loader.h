#pragma once
#include <filesystem>
#include "assets_library_model.h"


struct AssetsLoader 
{
public:
    static void load(const std::filesystem::path& rootPath, AssetsLibraryModel& library);

    static void loadPack(const std::filesystem::path& packPath, AssetsPackModel* model);
    static void saveGroup(const std::filesystem::path& rootPath, const std::string& groupName, const AssetsPackModel* model);

    static void loadAsset(const std::filesystem::path& assetPath, AssetsPackModel* model);
    
    //void saveAsset(const fs::path& groupPath, ImageAsset* asset) ;

    
};