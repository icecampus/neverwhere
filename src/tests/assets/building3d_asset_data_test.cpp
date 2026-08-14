// building3d asset payload: JSON (de)serialization in both domains — BaseData
// (editor) and game_data (runtime AssetIndex). Guards the GLB + albedo sidecar
// + 3x3 cell footprint defaults the placer/renderer rely on.
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "base_data/assets.h"
#include "game_data/assets.h"

namespace {

constexpr const char* kUuid = "5d46e934-edf2-4c27-81ac-bd6aad9900be";

nlohmann::json minimalPayload() {
    return nlohmann::json{
        {"uuid", kUuid},
        {"layerType", "GameplayInteractive"},
        {"pivot", {{"x", 0.0}, {"y", 0.0}}},
        {"building3d", nlohmann::json::object()},
    };
}

} // namespace

TEST(Building3dAssetData, BaseDataDefaultsWhenPayloadEmpty) {
    const BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();

    EXPECT_EQ(asset.layerType, LayerTypes::GameplayInteractive);
    ASSERT_TRUE(asset.building3dData.has_value());
    const BaseData::Building3dAssetData& d = *asset.building3dData;
    EXPECT_TRUE(d.thumbnail.empty());
    EXPECT_TRUE(d.model.empty());
    EXPECT_TRUE(d.albedo.empty());
    EXPECT_EQ(d.footprintWidth, 3);
    EXPECT_EQ(d.footprintHeight, 3);
    EXPECT_FLOAT_EQ(d.heightScale, 96.0f);
    EXPECT_FLOAT_EQ(d.yawDegrees, 0.0f);
    EXPECT_FLOAT_EQ(d.scale, 1.0f);
}

TEST(Building3dAssetData, BaseDataRoundTripPreservesAllFields) {
    BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();
    BaseData::Building3dAssetData& d = *asset.building3dData;
    d.thumbnail = "thumbnail.png";
    d.model = "home.glb";
    d.albedo = "albedo.png";
    d.footprintWidth = 5;
    d.footprintHeight = 3;
    d.heightScale = 64.0f;
    d.yawDegrees = 180.0f;
    d.scale = 1.25f;

    nlohmann::json j;
    BaseData::to_json(j, asset);
    const BaseData::AssetData back = j.get<BaseData::AssetData>();

    EXPECT_EQ(back.layerType, LayerTypes::GameplayInteractive);
    ASSERT_TRUE(back.building3dData.has_value());
    const BaseData::Building3dAssetData& b = *back.building3dData;
    EXPECT_EQ(b.thumbnail, "thumbnail.png");
    EXPECT_EQ(b.model, "home.glb");
    EXPECT_EQ(b.albedo, "albedo.png");
    EXPECT_EQ(b.footprintWidth, 5);
    EXPECT_EQ(b.footprintHeight, 3);
    EXPECT_FLOAT_EQ(b.heightScale, 64.0f);
    EXPECT_FLOAT_EQ(b.yawDegrees, 180.0f);
    EXPECT_FLOAT_EQ(b.scale, 1.25f);
}

TEST(Building3dAssetData, GameDataFromJson) {
    nlohmann::json j = minimalPayload();
    j["building3d"] = {
        {"thumbnail", "thumbnail.png"},
        {"model", "home.glb"},
        {"albedo", "albedo.png"},
        {"footprintWidth", 3},
        {"footprintHeight", 3},
        {"heightScale", 96.0},
        {"yawDegrees", 180.0},
        {"scale", 1.5},
    };
    const game_data::AssetData asset = j.get<game_data::AssetData>();

    EXPECT_EQ(asset.layerType, game_data::LayerType::GameplayInteractive);
    ASSERT_TRUE(asset.building3d.has_value());
    EXPECT_EQ(asset.building3d->model, "home.glb");
    EXPECT_EQ(asset.building3d->albedo, "albedo.png");
    EXPECT_EQ(asset.building3d->footprintWidth, 3);
    EXPECT_EQ(asset.building3d->footprintHeight, 3);
    EXPECT_FLOAT_EQ(asset.building3d->heightScale, 96.0f);
    EXPECT_FLOAT_EQ(asset.building3d->yawDegrees, 180.0f);
    EXPECT_FLOAT_EQ(asset.building3d->scale, 1.5f);

    const game_data::AssetData def = minimalPayload().get<game_data::AssetData>();
    ASSERT_TRUE(def.building3d.has_value());
    EXPECT_TRUE(def.building3d->model.empty());
    EXPECT_EQ(def.building3d->footprintWidth, 3);
    EXPECT_FLOAT_EQ(def.building3d->heightScale, 96.0f);
    EXPECT_FLOAT_EQ(def.building3d->yawDegrees, 0.0f);
    EXPECT_FLOAT_EQ(def.building3d->scale, 1.0f);
}

TEST(Building3dAssetData, AssetIndexLoadMapsBuildingEntry) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "building3d_asset_index_test";
    const std::filesystem::path assetDir = root / "pack" / "Home";
    std::filesystem::create_directories(assetDir);
    {
        nlohmann::json payload = minimalPayload();
        payload["building3d"]["model"] = "home.glb";
        payload["building3d"]["albedo"] = "albedo.png";
        payload["building3d"]["thumbnail"] = "thumbnail.png";
        payload["building3d"]["footprintWidth"] = 3;
        payload["building3d"]["footprintHeight"] = 3;
        payload["building3d"]["yawDegrees"] = 180.0;
        payload["building3d"]["scale"] = 1.1;
        std::ofstream file(assetDir / "index.json");
        file << payload.dump(2);
    }

    const game_data::AssetIndex index = game_data::AssetIndex::load(root);
    const game_data::AssetIndexEntry* entry = index.find(kUuid);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->isBuilding3d());
    EXPECT_EQ(entry->layerType, game_data::LayerType::GameplayInteractive);
    EXPECT_EQ(entry->building.model, "home.glb");
    EXPECT_EQ(entry->building.albedo, "albedo.png");
    EXPECT_EQ(entry->building.footprintWidth, 3);
    EXPECT_EQ(entry->building.footprintHeight, 3);
    EXPECT_FLOAT_EQ(entry->building.heightScale, 96.0f);
    EXPECT_FLOAT_EQ(entry->building.yawDegrees, 180.0f);
    EXPECT_FLOAT_EQ(entry->building.scale, 1.1f);
    EXPECT_EQ(entry->modelPath, assetDir / "home.glb");
    EXPECT_EQ(entry->albedoPath, assetDir / "albedo.png");
    EXPECT_FALSE(entry->isSlice());
    EXPECT_FALSE(entry->isImage());
    EXPECT_FALSE(entry->isMask3d());

    std::filesystem::remove_all(root);
}
