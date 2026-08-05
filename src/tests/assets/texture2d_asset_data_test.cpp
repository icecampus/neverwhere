// texture2d asset payload: JSON (de)serialization in both domains — BaseData
// (editor) and game_data (runtime AssetIndex). Guards the defaults the
// tiling-texture brush relies on (empty texture, 1 repeat per cell).
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
        {"layerType", "TextureLandscape"},
        {"pivot", {{"x", 0.0}, {"y", 0.0}}},
        {"texture2d", nlohmann::json::object()},
    };
}

} // namespace

TEST(Texture2dAssetData, BaseDataDefaultsWhenPayloadEmpty) {
    const BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();

    EXPECT_EQ(asset.layerType, LayerTypes::TextureLandscape);
    ASSERT_TRUE(asset.texture2dData.has_value());
    const BaseData::Texture2dAssetData& d = *asset.texture2dData;
    EXPECT_TRUE(d.thumbnail.empty());
    EXPECT_TRUE(d.texture.empty());
    EXPECT_FLOAT_EQ(d.tilingRepeats, 1.0f);
}

TEST(Texture2dAssetData, BaseDataRoundTripPreservesAllFields) {
    BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();
    BaseData::Texture2dAssetData& d = *asset.texture2dData;
    d.thumbnail = "thumbnail.png";
    d.texture = "texture.jpg";
    d.tilingRepeats = 2.5f;

    nlohmann::json j;
    BaseData::to_json(j, asset);
    const BaseData::AssetData back = j.get<BaseData::AssetData>();

    EXPECT_EQ(back.layerType, LayerTypes::TextureLandscape); // magic_enum name round-trip
    ASSERT_TRUE(back.texture2dData.has_value());
    const BaseData::Texture2dAssetData& b = *back.texture2dData;
    EXPECT_EQ(b.thumbnail, "thumbnail.png");
    EXPECT_EQ(b.texture, "texture.jpg");
    EXPECT_FLOAT_EQ(b.tilingRepeats, 2.5f);
}

TEST(Texture2dAssetData, GameDataFromJson) {
    nlohmann::json j = minimalPayload();
    j["texture2d"] = {
        {"thumbnail", "thumbnail.png"},
        {"texture", "texture.jpg"},
        {"tilingRepeats", 3.0},
    };
    const game_data::AssetData asset = j.get<game_data::AssetData>();

    EXPECT_EQ(asset.layerType, game_data::LayerType::TextureLandscape);
    ASSERT_TRUE(asset.texture2d.has_value());
    EXPECT_EQ(asset.texture2d->thumbnail, "thumbnail.png");
    EXPECT_EQ(asset.texture2d->texture, "texture.jpg");
    EXPECT_FLOAT_EQ(asset.texture2d->tilingRepeats, 3.0f);

    // Omitted fields keep the defaults.
    const game_data::AssetData def = minimalPayload().get<game_data::AssetData>();
    ASSERT_TRUE(def.texture2d.has_value());
    EXPECT_TRUE(def.texture2d->thumbnail.empty());
    EXPECT_TRUE(def.texture2d->texture.empty());
    EXPECT_FLOAT_EQ(def.texture2d->tilingRepeats, 1.0f);
}

TEST(Texture2dAssetData, AssetIndexLoadMapsTextureEntry) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "texture2d_asset_index_test";
    const std::filesystem::path assetDir = root / "pack" / "GrassTex";
    std::filesystem::create_directories(assetDir);
    {
        nlohmann::json payload = minimalPayload();
        payload["texture2d"]["thumbnail"] = "thumbnail.png";
        payload["texture2d"]["texture"] = "texture.jpg";
        payload["texture2d"]["tilingRepeats"] = 2.0;
        std::ofstream file(assetDir / "index.json");
        file << payload.dump(2);
    }

    const game_data::AssetIndex index = game_data::AssetIndex::load(root);
    const game_data::AssetIndexEntry* entry = index.find(kUuid);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->isTexture2d());
    EXPECT_EQ(entry->layerType, game_data::LayerType::TextureLandscape);
    EXPECT_EQ(entry->textureData.thumbnail, "thumbnail.png");
    EXPECT_EQ(entry->textureData.texture, "texture.jpg");
    EXPECT_FLOAT_EQ(entry->textureData.tilingRepeats, 2.0f);
    // The texture path resolves against the asset dir.
    EXPECT_EQ(entry->texturePath, assetDir / "texture.jpg");
    EXPECT_FALSE(entry->isSlice());
    EXPECT_FALSE(entry->isShape3d());
    EXPECT_FALSE(entry->isCliff3d());
    EXPECT_FALSE(entry->isCyclopean3d());
    EXPECT_FALSE(entry->isStone3d());

    std::filesystem::remove_all(root);
}
