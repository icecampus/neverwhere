// stone3d asset payload: JSON (de)serialization in both domains — BaseData
// (editor) and game_data (runtime AssetIndex). Guards the defaults the
// generator relies on (StoneFieldParams carve, ground slab off, flat top).
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "base_data/assets.h"
#include "game_data/assets.h"

namespace {

constexpr const char* kUuid = "dddddddd-1111-2222-3333-444444444444";

nlohmann::json minimalPayload() {
    return nlohmann::json{
        {"uuid", kUuid},
        {"layerType", "StoneLandscape"},
        {"pivot", {{"x", 0.0}, {"y", 0.0}}},
        {"stone3d", nlohmann::json::object()},
    };
}

} // namespace

TEST(Stone3dAssetData, BaseDataDefaultsWhenPayloadEmpty) {
    const BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();

    EXPECT_EQ(asset.layerType, LayerTypes::StoneLandscape);
    ASSERT_TRUE(asset.stone3dData.has_value());
    const BaseData::Stone3dAssetData& d = *asset.stone3dData;
    // Base slab defaults (cliff::FieldParams mirror).
    EXPECT_FLOAT_EQ(d.cellSize, 0.045f);
    EXPECT_FLOAT_EQ(d.raisedHeight, 96.0f);
    EXPECT_FALSE(d.groundEnabled); // standalone highground by default
    EXPECT_FLOAT_EQ(d.grooveAngles[1][0], 2.1991149f);
    // Stone carve defaults (stone_gen::StoneFieldParams mirror; the shared
    // field names take the StoneFieldParams values).
    EXPECT_FLOAT_EQ(d.voroScale, 2.0f);
    EXPECT_FLOAT_EQ(d.cellJitter, 1.0f);
    EXPECT_FLOAT_EQ(d.grooveDepth, 0.08f);
    EXPECT_FLOAT_EQ(d.grooveK, 2.5f);
    EXPECT_FLOAT_EQ(d.grooveMaskWidth, 0.25f);
    EXPECT_FLOAT_EQ(d.fbmAmplitude, 0.02f);
    EXPECT_FLOAT_EQ(d.fbmFrequency, 4.0f);
    EXPECT_FLOAT_EQ(d.seed, 0.0f);
    EXPECT_EQ(d.blurPasses, 2);
    EXPECT_TRUE(d.flatTop);
    EXPECT_FLOAT_EQ(d.flatTopLo, 0.55f);
    EXPECT_FLOAT_EQ(d.flatTopHi, 0.85f);
    EXPECT_FLOAT_EQ(d.rimWidth, 0.35f);
    EXPECT_FLOAT_EQ(d.rimBulge, 1.0f);
    EXPECT_FLOAT_EQ(d.rimNotch, 0.04f);
    // Shading palette + stone extras.
    EXPECT_FLOAT_EQ(d.shading.veinThreshold, 0.8f);
    EXPECT_FLOAT_EQ(d.shading.grassA[1], 0.62f);
    EXPECT_FLOAT_EQ(d.grassFade, 0.12f);
    EXPECT_FLOAT_EQ(d.rimShade, 1.0f);
    EXPECT_FLOAT_EQ(d.topTexMix, 1.0f);
    EXPECT_FLOAT_EQ(d.topTexTiles, 1.0f);
}

TEST(Stone3dAssetData, BaseDataRoundTripPreservesAllFields) {
    BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();
    BaseData::Stone3dAssetData& d = *asset.stone3dData;
    d.thumbnail = "thumb.png";
    d.topTexture = "../../textures/grass.png";
    d.raisedHeight = 64.0f;
    d.cellSize = 0.09f;
    d.blurPasses = 5;
    d.groundEnabled = true;
    d.grooveAngles[2][1] = 1.5f;
    d.voroScale = 3.5f;
    d.cellJitter = 0.7f;
    d.grooveDepth = 0.15f;
    d.grooveK = 4.0f;
    d.grooveMaskWidth = 0.4f;
    d.fbmAmplitude = 0.05f;
    d.fbmFrequency = 6.0f;
    d.seed = 2.5f;
    d.flatTop = false;
    d.flatTopLo = 0.4f;
    d.flatTopHi = 0.9f;
    d.rimWidth = 0.5f;
    d.rimBulge = 0.6f;
    d.rimNotch = 0.08f;
    d.shading.ambient = 0.55f;
    d.shading.darkColor = {0.1f, 0.2f, 0.3f};
    d.grassFade = 0.3f;
    d.rimShade = 0.7f;
    d.topTexMix = 0.8f;
    d.topTexTiles = 2.0f;

    nlohmann::json j;
    BaseData::to_json(j, asset);
    const BaseData::AssetData back = j.get<BaseData::AssetData>();

    EXPECT_EQ(back.layerType, LayerTypes::StoneLandscape); // magic_enum name round-trip
    ASSERT_TRUE(back.stone3dData.has_value());
    const BaseData::Stone3dAssetData& b = *back.stone3dData;
    EXPECT_EQ(b.thumbnail, "thumb.png");
    EXPECT_EQ(b.topTexture, "../../textures/grass.png");
    EXPECT_FLOAT_EQ(b.raisedHeight, 64.0f);
    EXPECT_FLOAT_EQ(b.cellSize, 0.09f);
    EXPECT_EQ(b.blurPasses, 5);
    EXPECT_TRUE(b.groundEnabled);
    EXPECT_FLOAT_EQ(b.grooveAngles[2][1], 1.5f);
    EXPECT_FLOAT_EQ(b.voroScale, 3.5f);
    EXPECT_FLOAT_EQ(b.cellJitter, 0.7f);
    EXPECT_FLOAT_EQ(b.grooveDepth, 0.15f);
    EXPECT_FLOAT_EQ(b.grooveK, 4.0f);
    EXPECT_FLOAT_EQ(b.grooveMaskWidth, 0.4f);
    EXPECT_FLOAT_EQ(b.fbmAmplitude, 0.05f);
    EXPECT_FLOAT_EQ(b.fbmFrequency, 6.0f);
    EXPECT_FLOAT_EQ(b.seed, 2.5f);
    EXPECT_FALSE(b.flatTop);
    EXPECT_FLOAT_EQ(b.flatTopLo, 0.4f);
    EXPECT_FLOAT_EQ(b.flatTopHi, 0.9f);
    EXPECT_FLOAT_EQ(b.rimWidth, 0.5f);
    EXPECT_FLOAT_EQ(b.rimBulge, 0.6f);
    EXPECT_FLOAT_EQ(b.rimNotch, 0.08f);
    EXPECT_FLOAT_EQ(b.shading.ambient, 0.55f);
    EXPECT_FLOAT_EQ(b.shading.darkColor[0], 0.1f);
    EXPECT_FLOAT_EQ(b.shading.darkColor[2], 0.3f);
    EXPECT_FLOAT_EQ(b.grassFade, 0.3f);
    EXPECT_FLOAT_EQ(b.rimShade, 0.7f);
    EXPECT_FLOAT_EQ(b.topTexMix, 0.8f);
    EXPECT_FLOAT_EQ(b.topTexTiles, 2.0f);
}

TEST(Stone3dAssetData, GameDataFromJson) {
    nlohmann::json j = minimalPayload();
    j["stone3d"] = {
        {"raisedHeight", 48.0},
        {"cellSize", 0.06},
        {"groundEnabled", true},
        {"topTexture", "../../textures/grass.png"},
        {"voroScale", 1.5},
        {"grooveDepth", 0.12},
        {"grooveK", 3.0},
        {"seed", 1.25},
        {"blurPasses", 4},
        {"flatTop", false},
        {"rimWidth", 0.45},
        {"rimBulge", 0.5},
        {"rimNotch", 0.06},
        {"shading", {{"diffuse", 0.9}, {"goldColor", {0.7, 0.6, 0.4}}}},
        {"grassFade", 0.2},
        {"rimShade", 0.6},
        {"topTexMix", 0.7},
        {"topTexTiles", 3.0},
    };
    const game_data::AssetData asset = j.get<game_data::AssetData>();

    EXPECT_EQ(asset.layerType, game_data::LayerType::StoneLandscape);
    ASSERT_TRUE(asset.stone3d.has_value());
    EXPECT_FLOAT_EQ(asset.stone3d->raisedHeight, 48.0f);
    EXPECT_FLOAT_EQ(asset.stone3d->cellSize, 0.06f);
    EXPECT_TRUE(asset.stone3d->groundEnabled);
    EXPECT_EQ(asset.stone3d->topTexture, "../../textures/grass.png");
    EXPECT_FLOAT_EQ(asset.stone3d->voroScale, 1.5f);
    EXPECT_FLOAT_EQ(asset.stone3d->grooveDepth, 0.12f);
    EXPECT_FLOAT_EQ(asset.stone3d->grooveK, 3.0f);
    EXPECT_FLOAT_EQ(asset.stone3d->seed, 1.25f);
    EXPECT_EQ(asset.stone3d->blurPasses, 4);
    EXPECT_FALSE(asset.stone3d->flatTop);
    EXPECT_FLOAT_EQ(asset.stone3d->rimWidth, 0.45f);
    EXPECT_FLOAT_EQ(asset.stone3d->rimBulge, 0.5f);
    EXPECT_FLOAT_EQ(asset.stone3d->rimNotch, 0.06f);
    EXPECT_FLOAT_EQ(asset.stone3d->shading.diffuse, 0.9f);
    EXPECT_FLOAT_EQ(asset.stone3d->shading.goldColor[2], 0.4f);
    EXPECT_FLOAT_EQ(asset.stone3d->grassFade, 0.2f);
    EXPECT_FLOAT_EQ(asset.stone3d->rimShade, 0.6f);
    EXPECT_FLOAT_EQ(asset.stone3d->topTexMix, 0.7f);
    EXPECT_FLOAT_EQ(asset.stone3d->topTexTiles, 3.0f);
    // Omitted fields keep the generator defaults.
    EXPECT_FLOAT_EQ(asset.stone3d->cellJitter, 1.0f);
    EXPECT_FLOAT_EQ(asset.stone3d->flatTopLo, 0.55f);
    EXPECT_FLOAT_EQ(asset.stone3d->flatTopHi, 0.85f);
    EXPECT_FLOAT_EQ(asset.stone3d->fbmAmplitude, 0.02f);
    EXPECT_FLOAT_EQ(asset.stone3d->shading.bottomBand, 0.35f);
}

TEST(Stone3dAssetData, AssetIndexLoadMapsStoneEntry) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "stone3d_asset_index_test";
    const std::filesystem::path assetDir = root / "pack" / "StoneRock";
    std::filesystem::create_directories(assetDir);
    {
        nlohmann::json payload = minimalPayload();
        payload["stone3d"]["topTexture"] = "../../textures/grass.png";
        std::ofstream file(assetDir / "index.json");
        file << payload.dump(2);
    }

    const game_data::AssetIndex index = game_data::AssetIndex::load(root);
    const game_data::AssetIndexEntry* entry = index.find(kUuid);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->isStone3d());
    EXPECT_EQ(entry->layerType, game_data::LayerType::StoneLandscape);
    EXPECT_FLOAT_EQ(entry->stone.raisedHeight, 96.0f);
    EXPECT_FLOAT_EQ(entry->stone.voroScale, 2.0f);
    EXPECT_TRUE(entry->stone.flatTop);
    EXPECT_FALSE(entry->stone.groundEnabled);
    EXPECT_FALSE(entry->isSlice());
    EXPECT_FALSE(entry->isShape3d());
    EXPECT_FALSE(entry->isCliff3d());
    EXPECT_FALSE(entry->isCyclopean3d());
    // The stone's topTexture resolves against the asset dir (shared slot).
    EXPECT_EQ(entry->topTexturePath, assetDir / "../../textures/grass.png");

    std::filesystem::remove_all(root);
}
