// cliff3d asset payload: JSON (de)serialization in both domains — BaseData
// (editor) and game_data (runtime AssetIndex). Guards the defaults the
// renderer relies on (ground slab off, playground shading palette).
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "base_data/assets.h"
#include "game_data/assets.h"

namespace {

constexpr const char* kUuid = "cccccccc-1111-2222-3333-444444444444";

nlohmann::json minimalPayload() {
    return nlohmann::json{
        {"uuid", kUuid},
        {"layerType", "CliffLandscape"},
        {"pivot", {{"x", 0.0}, {"y", 0.0}}},
        {"cliff3d", nlohmann::json::object()},
    };
}

} // namespace

TEST(Cliff3dAssetData, BaseDataDefaultsWhenPayloadEmpty) {
    const BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();

    EXPECT_EQ(asset.layerType, LayerTypes::CliffLandscape);
    ASSERT_TRUE(asset.cliff3dData.has_value());
    const BaseData::Cliff3dAssetData& d = *asset.cliff3dData;
    EXPECT_FLOAT_EQ(d.cellSize, 0.045f);
    EXPECT_FLOAT_EQ(d.raisedHeight, 96.0f);
    EXPECT_FALSE(d.groundEnabled); // standalone highground by default
    EXPECT_EQ(d.fbmOctaves, 2);
    EXPECT_FLOAT_EQ(d.grooveAngles[1][0], 2.1991149f);
    EXPECT_FLOAT_EQ(d.shading.veinThreshold, 0.8f);
    EXPECT_FLOAT_EQ(d.shading.grassA[1], 0.62f);
}

TEST(Cliff3dAssetData, BaseDataRoundTripPreservesAllFields) {
    BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();
    BaseData::Cliff3dAssetData& d = *asset.cliff3dData;
    d.thumbnail = "thumb.png";
    d.raisedHeight = 64.0f;
    d.cellSize = 0.09f;
    d.blurPasses = 5;
    d.groundEnabled = true;
    d.grooveAngles[2][1] = 1.5f;
    d.shading.ambient = 0.55f;
    d.shading.darkColor = {0.1f, 0.2f, 0.3f};

    nlohmann::json j;
    BaseData::to_json(j, asset);
    const BaseData::AssetData back = j.get<BaseData::AssetData>();

    EXPECT_EQ(back.layerType, LayerTypes::CliffLandscape); // magic_enum name round-trip
    ASSERT_TRUE(back.cliff3dData.has_value());
    const BaseData::Cliff3dAssetData& b = *back.cliff3dData;
    EXPECT_EQ(b.thumbnail, "thumb.png");
    EXPECT_FLOAT_EQ(b.raisedHeight, 64.0f);
    EXPECT_FLOAT_EQ(b.cellSize, 0.09f);
    EXPECT_EQ(b.blurPasses, 5);
    EXPECT_TRUE(b.groundEnabled);
    EXPECT_FLOAT_EQ(b.grooveAngles[2][1], 1.5f);
    EXPECT_FLOAT_EQ(b.shading.ambient, 0.55f);
    EXPECT_FLOAT_EQ(b.shading.darkColor[0], 0.1f);
    EXPECT_FLOAT_EQ(b.shading.darkColor[2], 0.3f);
}

TEST(Cliff3dAssetData, GameDataFromJson) {
    nlohmann::json j = minimalPayload();
    j["cliff3d"] = {
        {"raisedHeight", 48.0},
        {"cellSize", 0.06},
        {"groundEnabled", true},
        {"grooveSmooth", 0.05},
        {"shading", {{"diffuse", 0.9}, {"goldColor", {0.7, 0.6, 0.4}}}},
    };
    const game_data::AssetData asset = j.get<game_data::AssetData>();

    EXPECT_EQ(asset.layerType, game_data::LayerType::CliffLandscape);
    ASSERT_TRUE(asset.cliff3d.has_value());
    EXPECT_FLOAT_EQ(asset.cliff3d->raisedHeight, 48.0f);
    EXPECT_FLOAT_EQ(asset.cliff3d->cellSize, 0.06f);
    EXPECT_TRUE(asset.cliff3d->groundEnabled);
    EXPECT_FLOAT_EQ(asset.cliff3d->grooveSmooth, 0.05f);
    EXPECT_FLOAT_EQ(asset.cliff3d->shading.diffuse, 0.9f);
    EXPECT_FLOAT_EQ(asset.cliff3d->shading.goldColor[2], 0.4f);
    // Omitted fields keep the generator defaults.
    EXPECT_FLOAT_EQ(asset.cliff3d->groovePeriod, 0.4f);
    EXPECT_EQ(asset.cliff3d->fbmOctaves, 2);
}

TEST(Cliff3dAssetData, AssetIndexLoadMapsCliffEntry) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cliff3d_asset_index_test";
    const std::filesystem::path assetDir = root / "pack" / "CliffRock";
    std::filesystem::create_directories(assetDir);
    {
        std::ofstream file(assetDir / "index.json");
        file << minimalPayload().dump(2);
    }

    const game_data::AssetIndex index = game_data::AssetIndex::load(root);
    const game_data::AssetIndexEntry* entry = index.find(kUuid);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->isCliff3d());
    EXPECT_EQ(entry->layerType, game_data::LayerType::CliffLandscape);
    EXPECT_FLOAT_EQ(entry->cliff.raisedHeight, 96.0f);
    EXPECT_FALSE(entry->cliff.groundEnabled);
    EXPECT_FALSE(entry->isSlice());
    EXPECT_FALSE(entry->isShape3d());

    std::filesystem::remove_all(root);
}
