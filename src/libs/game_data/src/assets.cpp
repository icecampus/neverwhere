#include "game_data/assets.h"

#include <fstream>

namespace game_data {
namespace fs = std::filesystem;

AssetIndex AssetIndex::load(const fs::path& assetsRoot) {
    AssetIndex index;
    if (!fs::exists(assetsRoot)) {
        throw std::runtime_error("Assets root does not exist: " + assetsRoot.string());
    }

    for (const auto& entry : fs::recursive_directory_iterator(assetsRoot)) {
        if (!entry.is_regular_file()) continue;
        if (entry.path().filename() != "index.json") continue;

        std::ifstream file(entry.path());
        if (!file.is_open()) continue;

        nlohmann::json j;
        try {
            file >> j;
        } catch (...) {
            continue;
        }

        if (!j.contains("uuid")) continue;

        AssetData asset = j.get<AssetData>();
        asset.indexPath = entry.path();
        if (!asset.slice && !asset.image && !asset.shape3d && !asset.cliff3d) continue; // nothing renderable

        AssetIndexEntry idx;
        idx.uuid = asset.uuid;
        idx.layerType = asset.layerType;
        idx.pivot = asset.pivot;

        if (asset.slice) {
            idx.atlasPath = asset.root() / asset.slice->atlas;

            // Convention from editor: split atlas into 4x6 tiles
            idx.cols = 4;
            idx.rows = 6;
        }

        if (asset.shape3d) {
            idx.atlasPath = asset.root() / asset.shape3d->atlas;
            idx.cols = 4;
            idx.rows = 6;
            idx.shape3d = true;
            idx.raisedHeight = asset.shape3d->raisedHeight;
            idx.rockWalls = asset.shape3d->rockWalls;
            idx.rockAmplitude = asset.shape3d->rockAmplitude;
            idx.rockBevel = asset.shape3d->rockBevel;
            if (!asset.shape3d->topTexture.empty()) {
                idx.topTexturePath = asset.root() / asset.shape3d->topTexture;
            }
        }

        if (asset.image) {
            idx.imagePath = asset.root() / asset.image->imageFilename;
            idx.widthCells = asset.image->width;
        }

        if (asset.cliff3d) {
            idx.cliff3d = true;
            idx.cliff = *asset.cliff3d;
            // Shares the entry's texture slot with shape3d (an asset is one
            // kind or the other).
            if (!asset.cliff3d->topTexture.empty()) {
                idx.topTexturePath = asset.root() / asset.cliff3d->topTexture;
            }
        }

        index.byUuid[idx.uuid] = idx;
    }

    return index;
}

const AssetIndexEntry* AssetIndex::find(const std::string& uuid) const {
    auto it = byUuid.find(uuid);
    if (it == byUuid.end()) return nullptr;
    return &it->second;
}

} // namespace game_data

