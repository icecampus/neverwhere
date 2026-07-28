// cyclopean3d asset payload: JSON (de)serialization in both domains —
// BaseData (editor) and game_data (runtime AssetIndex). Guards the defaults
// the CyclopeanRenderer relies on (Cyclopean wall style composer params).
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "base_data/assets.h"
#include "game_data/assets.h"

namespace {

constexpr const char* kUuid = "eeeeeeee-1111-2222-3333-444444444444";

nlohmann::json minimalPayload() {
    return nlohmann::json{
        {"uuid", kUuid},
        {"layerType", "CyclopeanLandscape"},
        {"pivot", {{"x", 0.0}, {"y", 0.0}}},
        {"cyclopean3d", nlohmann::json::object()},
    };
}

} // namespace

TEST(Cyclopean3dAssetData, BaseDataDefaultsWhenPayloadEmpty) {
    const BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();

    EXPECT_EQ(asset.layerType, LayerTypes::CyclopeanLandscape);
    ASSERT_TRUE(asset.cyclopean3dData.has_value());
    const BaseData::Cyclopean3dAssetData& d = *asset.cyclopean3dData;
    EXPECT_FLOAT_EQ(d.raisedHeight, 3.0f);
    EXPECT_EQ(d.rockSeed, 1337);
    EXPECT_FLOAT_EQ(d.rockAmplitude, 0.28f);
    EXPECT_TRUE(d.rockEnabled);
    EXPECT_FLOAT_EQ(d.cornerBevel, 0.3f);
    EXPECT_EQ(d.wallSubdivH, 16);
    EXPECT_EQ(d.wallSubdivV, 16);
}

TEST(Cyclopean3dAssetData, BaseDataRoundTripPreservesAllFields) {
    BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();
    BaseData::Cyclopean3dAssetData& d = *asset.cyclopean3dData;
    d.thumbnail = "thumb.png";
    d.raisedHeight = 5.5f;
    d.rockSeed = 42;
    d.rockAmplitude = 0.6f;
    d.rockEnabled = false;
    d.cornerBevel = 0.15f;
    d.wallSubdivH = 8;
    d.wallSubdivV = 12;

    nlohmann::json j;
    BaseData::to_json(j, asset);
    const BaseData::AssetData back = j.get<BaseData::AssetData>();

    EXPECT_EQ(back.layerType, LayerTypes::CyclopeanLandscape); // magic_enum name round-trip
    ASSERT_TRUE(back.cyclopean3dData.has_value());
    const BaseData::Cyclopean3dAssetData& b = *back.cyclopean3dData;
    EXPECT_EQ(b.thumbnail, "thumb.png");
    EXPECT_FLOAT_EQ(b.raisedHeight, 5.5f);
    EXPECT_EQ(b.rockSeed, 42);
    EXPECT_FLOAT_EQ(b.rockAmplitude, 0.6f);
    EXPECT_FALSE(b.rockEnabled);
    EXPECT_FLOAT_EQ(b.cornerBevel, 0.15f);
    EXPECT_EQ(b.wallSubdivH, 8);
    EXPECT_EQ(b.wallSubdivV, 12);
}

TEST(Cyclopean3dAssetData, GameDataFromJson) {
    nlohmann::json j = minimalPayload();
    j["cyclopean3d"] = {
        {"raisedHeight", 4.5},
        {"rockSeed", 77},
        {"rockAmplitude", 0.5},
        {"rockEnabled", false},
        {"cornerBevel", 0.2},
        {"wallSubdivH", 6},
    };
    const game_data::AssetData asset = j.get<game_data::AssetData>();

    EXPECT_EQ(asset.layerType, game_data::LayerType::CyclopeanLandscape);
    ASSERT_TRUE(asset.cyclopean3d.has_value());
    EXPECT_FLOAT_EQ(asset.cyclopean3d->raisedHeight, 4.5f);
    EXPECT_EQ(asset.cyclopean3d->rockSeed, 77);
    EXPECT_FLOAT_EQ(asset.cyclopean3d->rockAmplitude, 0.5f);
    EXPECT_FALSE(asset.cyclopean3d->rockEnabled);
    EXPECT_FLOAT_EQ(asset.cyclopean3d->cornerBevel, 0.2f);
    EXPECT_EQ(asset.cyclopean3d->wallSubdivH, 6);
    // Omitted fields keep the composer defaults.
    EXPECT_EQ(asset.cyclopean3d->wallSubdivV, 16);
}

TEST(Cyclopean3dAssetData, AssetIndexLoadMapsCyclopeanEntry) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "cyclopean3d_asset_index_test";
    const std::filesystem::path assetDir = root / "pack" / "CyclopeanWalls";
    std::filesystem::create_directories(assetDir);
    {
        std::ofstream file(assetDir / "index.json");
        file << minimalPayload().dump(2);
    }

    const game_data::AssetIndex index = game_data::AssetIndex::load(root);
    const game_data::AssetIndexEntry* entry = index.find(kUuid);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->isCyclopean3d());
    EXPECT_EQ(entry->layerType, game_data::LayerType::CyclopeanLandscape);
    EXPECT_FLOAT_EQ(entry->cyclopean.raisedHeight, 3.0f);
    EXPECT_EQ(entry->cyclopean.rockSeed, 1337);
    EXPECT_FALSE(entry->isSlice());
    EXPECT_FALSE(entry->isShape3d());
    EXPECT_FALSE(entry->isCliff3d());

    std::filesystem::remove_all(root);
}
