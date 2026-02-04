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
        if (!j.contains("slice")) continue; // MVP: only slice assets for landscape

        AssetData asset = j.get<AssetData>();
        asset.indexPath = entry.path();
        if (!asset.slice) continue;

        AssetIndexEntry idx;
        idx.uuid = asset.uuid;
        idx.layerType = asset.layerType;
        idx.atlasPath = asset.root() / asset.slice->atlas;

        // Convention from editor: split atlas into 4x6 tiles
        idx.cols = 4;
        idx.rows = 6;

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

