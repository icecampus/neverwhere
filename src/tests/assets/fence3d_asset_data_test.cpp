// fence3d asset payload: JSON (de)serialization in both domains — BaseData
// (editor) and game_data (runtime AssetIndex). Guards the defaults the fence
// brush relies on (empty thumbnail, 96 pt per meter lift) and the AssetIndex
// mesh-dir resolution against the bundle root.
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "base_data/assets.h"
#include "game_data/assets.h"

namespace {

constexpr const char* kUuid = "fefefefe-1111-2222-3333-444444444444";

nlohmann::json minimalPayload() {
    return nlohmann::json{
        {"uuid", kUuid},
        {"layerType", "FenceLandscape"},
        {"pivot", {{"x", 0.0}, {"y", 0.0}}},
        {"fence3d", nlohmann::json::object()},
    };
}

} // namespace

TEST(Fence3dAssetData, BaseDataDefaultsWhenPayloadEmpty) {
    const BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();

    EXPECT_EQ(asset.layerType, LayerTypes::FenceLandscape);
    ASSERT_TRUE(asset.fence3dData.has_value());
    const BaseData::Fence3dAssetData& d = *asset.fence3dData;
    EXPECT_TRUE(d.thumbnail.empty());
    EXPECT_FLOAT_EQ(d.metersToPoints, 96.0f);
}

TEST(Fence3dAssetData, BaseDataRoundTripPreservesAllFields) {
    BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();
    BaseData::Fence3dAssetData& d = *asset.fence3dData;
    d.thumbnail = "thumbnail.png";
    d.metersToPoints = 128.0f;

    nlohmann::json j;
    BaseData::to_json(j, asset);
    const BaseData::AssetData back = j.get<BaseData::AssetData>();

    EXPECT_EQ(back.layerType, LayerTypes::FenceLandscape); // magic_enum name round-trip
    ASSERT_TRUE(back.fence3dData.has_value());
    const BaseData::Fence3dAssetData& b = *back.fence3dData;
    EXPECT_EQ(b.thumbnail, "thumbnail.png");
    EXPECT_FLOAT_EQ(b.metersToPoints, 128.0f);
}

TEST(Fence3dAssetData, GameDataFromJson) {
    nlohmann::json j = minimalPayload();
    j["fence3d"] = {
        {"thumbnail", "thumbnail.png"},
        {"metersToPoints", 72.0},
    };
    const game_data::AssetData asset = j.get<game_data::AssetData>();

    EXPECT_EQ(asset.layerType, game_data::LayerType::FenceLandscape);
    ASSERT_TRUE(asset.fence3d.has_value());
    EXPECT_EQ(asset.fence3d->thumbnail, "thumbnail.png");
    EXPECT_FLOAT_EQ(asset.fence3d->metersToPoints, 72.0f);

    // Omitted fields keep the defaults.
    const game_data::AssetData def = minimalPayload().get<game_data::AssetData>();
    ASSERT_TRUE(def.fence3d.has_value());
    EXPECT_TRUE(def.fence3d->thumbnail.empty());
    EXPECT_FLOAT_EQ(def.fence3d->metersToPoints, 96.0f);
}

TEST(Fence3dAssetData, AssetIndexLoadMapsFenceEntry) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "fence3d_asset_index_test";
    const std::filesystem::path assetDir = root / "Fence&Path" / "FenceVerdigris";
    std::filesystem::create_directories(assetDir);
    {
        nlohmann::json payload = minimalPayload();
        payload["fence3d"]["thumbnail"] = "thumbnail.png";
        payload["fence3d"]["metersToPoints"] = 80.0;
        std::ofstream file(assetDir / "index.json");
        file << payload.dump(2);
    }

    const game_data::AssetIndex index = game_data::AssetIndex::load(root);
    const game_data::AssetIndexEntry* entry = index.find(kUuid);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->isFence3d());
    EXPECT_EQ(entry->layerType, game_data::LayerType::FenceLandscape);
    EXPECT_EQ(entry->fence.thumbnail, "thumbnail.png");
    EXPECT_FLOAT_EQ(entry->fence.metersToPoints, 80.0f);
    // The mesh dir resolves against the bundle root (the conventional
    // fence_{post,corner,section2,section3}.obj live there).
    EXPECT_EQ(entry->meshDir, assetDir);
    EXPECT_FALSE(entry->isSlice());
    EXPECT_FALSE(entry->isShape3d());
    EXPECT_FALSE(entry->isCliff3d());
    EXPECT_FALSE(entry->isMask3d());

    std::filesystem::remove_all(root);
}
