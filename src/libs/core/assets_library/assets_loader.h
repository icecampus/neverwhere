#pragma once
#include <filesystem>
#include "assets_library_model.h"


struct AssetsLoader 
{
public:
    static void load(const std::filesystem::path& rootPath, AssetsLibraryModel& library);
    static void loadPack(const BaseData::AssetsPack& packData, AssetsPackModel* model);
    static void loadAsset(const BaseData::AssetData& assetData, AssetsPackModel* model);
    
    //void saveAsset(const fs::path& groupPath, ImageAsset* asset) ;

    
};