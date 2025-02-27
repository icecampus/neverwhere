#pragma once
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include "asset.h"       
#include "asset_model.h" 

using json = nlohmann::json;
namespace fs = std::filesystem;

class AssetManager 
{
public:
    explicit AssetManager(const fs::path& rootPath) : m_rootPath(rootPath) {
        if (!fs::exists(m_rootPath)) {
            std::cerr << "The root directory does not exist: " << m_rootPath << std::endl;
        }
    }

    void loadGroup(const std::string& groupName, AssetModel* model) {
        fs::path groupPath = m_rootPath / groupName;

        if (!fs::exists(groupPath)) {
            std::cerr << "Group directory does not exist: " << groupPath << std::endl;
            return;
        }

        for (const auto& entry : fs::directory_iterator(groupPath)) {
            if (entry.is_directory()) {
                loadAsset(entry.path(), model);
            }
        }
    }

    void saveGroup(const std::string& groupName, const AssetModel* model) {
        fs::path groupPath = m_rootPath / groupName;

        if (!fs::exists(groupPath)) {
            if (!fs::create_directories(groupPath)) {
                std::cerr << "Failed to create group directory: " << groupPath << std::endl;
                return;
            }
        }

        for (int i = 0; i < model->rowCount(); ++i) {
            QModelIndex index = model->index(i);
            Asset* asset = model->data(index, AssetModel::ElementRole).value<Asset*>();
            if (asset) {
                saveAsset(groupPath, asset);
            }
        }
    }

private:
    void loadAsset(const fs::path& assetPath, AssetModel* model);

    void saveAsset(const fs::path& groupPath, Asset* asset) {
        std::string assetDirName = asset->uuid().toString(QUuid::WithoutBraces).toStdString();
        fs::path assetPath = groupPath / assetDirName;

        if (!fs::exists(assetPath)) {
            if (!fs::create_directory(assetPath)) {
                std::cerr << "Failed to create asset directory: " << assetPath << std::endl;
                return;
            }
        }

        fs::path jsonFilePath = assetPath / "index.json";
        std::ofstream jsonFile(jsonFilePath);
        if (!jsonFile.is_open()) {
            std::cerr << "Failed to open JSON file for writing: " << jsonFilePath << std::endl;
            return;
        }

        json j = asset->toJson();
        jsonFile << j.dump(4); 
    }

    fs::path m_rootPath; 
};