#include "asset_manager.h"

void AssetManager::loadAsset(const fs::path& assetPath, AssetModel* model)
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
    auto asset = std::make_unique<Asset>(jsonFilePath, j, assetPath.string());
    model->addAsset(std::move(asset));
}