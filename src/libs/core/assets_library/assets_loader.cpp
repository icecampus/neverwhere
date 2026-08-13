#include "assets_loader.h"
#include <fstream>
#include <magic_enum/magic_enum.hpp>

#include "assets/image_asset.h"
#include "assets/slice_asset.h"
#include "assets/shape3d_asset.h"
#include "assets/cliff_asset.h"
#include "assets/cyclopean_asset.h"
#include "assets/stone_asset.h"
#include "assets/texture_asset.h"
#include "assets/tech_asset.h"
#include "assets/mask_asset.h"
#include "assets/fence_asset.h"
#include "assets_pack_model.h"
#include "base_data/lib.h"

namespace fs = std::filesystem;

void AssetsLoader::load(const std::filesystem::path& rootPath, AssetsLibraryModel& library)
{
   BaseData::Assets assetsData = BaseData::Assets::load(rootPath);

   for (const BaseData::AssetsPack& packData : assetsData)
   {
       library.addElement<AssetsPackModel>(packData.path, &library);
       AssetsPackModel* lastPack = library.element(library.size() - 1);
       loadPack(packData, lastPack);

   }
}

void AssetsLoader::loadPack(const BaseData::AssetsPack& packData, AssetsPackModel* model)
{
    for (const BaseData::AssetData& assetData: packData) 
    {
        loadAsset(assetData, model);
    }
}

void AssetsLoader::loadAsset(const BaseData::AssetData& assetData, AssetsPackModel* pack)
{
    if ( assetData.imageData.has_value() )
    {
        auto asset = std::make_unique<ImageAsset>(pack);
        asset->load(assetData);
        pack->addElement(std::move(asset));
    }

    if (assetData.sliceData.has_value() )
    {
        auto asset = std::make_unique<SliceAsset>(pack);
        asset->load(assetData);
        pack->addElement(std::move(asset));
    }

    if (assetData.shape3dData.has_value() )
    {
        auto asset = std::make_unique<Shape3dAsset>(pack);
        asset->load(assetData);
        pack->addElement(std::move(asset));
    }

    if (assetData.cliff3dData.has_value() )
    {
        auto asset = std::make_unique<CliffAsset>(pack);
        asset->load(assetData);
        pack->addElement(std::move(asset));
    }

    if (assetData.cyclopean3dData.has_value() )
    {
        auto asset = std::make_unique<CyclopeanAsset>(pack);
        asset->load(assetData);
        pack->addElement(std::move(asset));
    }

    if (assetData.stone3dData.has_value() )
    {
        auto asset = std::make_unique<StoneAsset>(pack);
        asset->load(assetData);
        pack->addElement(std::move(asset));
    }

    if (assetData.texture2dData.has_value() )
    {
        auto asset = std::make_unique<TextureAsset>(pack);
        asset->load(assetData);
        pack->addElement(std::move(asset));
    }

    if (assetData.tech3dData.has_value() )
    {
        auto asset = std::make_unique<TechAsset>(pack);
        asset->load(assetData);
        pack->addElement(std::move(asset));
    }

    if (assetData.mask3dData.has_value() )
    {
        auto asset = std::make_unique<MaskAsset>(pack);
        asset->load(assetData);
        pack->addElement(std::move(asset));
    }

    if (assetData.fence3dData.has_value() )
    {
        auto asset = std::make_unique<FenceAsset>(pack);
        asset->load(assetData);
        pack->addElement(std::move(asset));
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
