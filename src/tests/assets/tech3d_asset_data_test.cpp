// tech3d asset payload: JSON (de)serialization in both domains — BaseData
// (editor) and game_data (runtime AssetIndex). Guards the defaults the
// generator relies on (TechFieldParams, the TechnicalGrass earth palette).
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "base_data/assets.h"
#include "game_data/assets.h"

namespace {

constexpr const char* kUuid = "dddddddd-5555-6666-7777-888888888888";

nlohmann::json minimalPayload() {
    return nlohmann::json{
        {"uuid", kUuid},
        {"layerType", "TechLandscape"},
        {"pivot", {{"x", 0.0}, {"y", 0.0}}},
        {"tech3d", nlohmann::json::object()},
    };
}

} // namespace

TEST(Tech3dAssetData, BaseDataDefaultsWhenPayloadEmpty) {
    const BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();

    EXPECT_EQ(asset.layerType, LayerTypes::TechLandscape);
    ASSERT_TRUE(asset.tech3dData.has_value());
    const BaseData::Tech3dAssetData& d = *asset.tech3dData;
    // Generator defaults (tech::TechFieldParams mirror).
    EXPECT_FLOAT_EQ(d.cellSize, 0.06f);
    EXPECT_FLOAT_EQ(d.padding, 0.5f);
    EXPECT_FLOAT_EQ(d.levelHeight, 0.35f);
    EXPECT_FLOAT_EQ(d.groundDepth, 0.05f);
    EXPECT_FLOAT_EQ(d.style, 0.0f); // ridge by default
    EXPECT_FLOAT_EQ(d.soften, 0.0f);
    EXPECT_FLOAT_EQ(d.creaseWidth, 0.05f);
    EXPECT_EQ(d.blurPasses, 1);
    EXPECT_FLOAT_EQ(d.outlineDepth, 0.0f); // shoreline outline off by default
    EXPECT_FLOAT_EQ(d.raisedHeight, 96.0f);
    // TechnicalGrass palette: earth ramps, no veins, muted spec, flat-lit.
    EXPECT_FLOAT_EQ(d.shading.darkColor[0], 0.25f);
    EXPECT_FLOAT_EQ(d.shading.goldColor[0], 0.58f);
    EXPECT_FLOAT_EQ(d.shading.veinThreshold, 2.0f);
    EXPECT_FLOAT_EQ(d.shading.specStrength, 0.15f);
    EXPECT_FLOAT_EQ(d.shading.bottomDarken, 0.0f);
    EXPECT_FLOAT_EQ(d.shading.grassA[1], 0.62f);
}

TEST(Tech3dAssetData, BaseDataRoundTripPreservesAllFields) {
    BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();
    BaseData::Tech3dAssetData& d = *asset.tech3dData;
    d.thumbnail = "thumb.png";
    d.raisedHeight = 64.0f;
    d.cellSize = 0.09f;
    d.padding = 0.7f;
    d.levelHeight = 0.5f;
    d.groundDepth = 0.12f;
    d.style = 1.0f; // full valley
    d.soften = 0.6f;
    d.creaseWidth = 0.0f; // contour off
    d.blurPasses = 2;
    d.outlineDepth = 1.5f; // shoreline outline on, one and a half levels down
    d.shading.ambient = 0.55f;
    d.shading.darkColor = {0.1f, 0.2f, 0.3f};
    d.shading.bottomDarken = 0.4f;

    nlohmann::json j;
    BaseData::to_json(j, asset);
    const BaseData::AssetData back = j.get<BaseData::AssetData>();

    EXPECT_EQ(back.layerType, LayerTypes::TechLandscape); // magic_enum name round-trip
    ASSERT_TRUE(back.tech3dData.has_value());
    const BaseData::Tech3dAssetData& b = *back.tech3dData;
    EXPECT_EQ(b.thumbnail, "thumb.png");
    EXPECT_FLOAT_EQ(b.raisedHeight, 64.0f);
    EXPECT_FLOAT_EQ(b.cellSize, 0.09f);
    EXPECT_FLOAT_EQ(b.padding, 0.7f);
    EXPECT_FLOAT_EQ(b.levelHeight, 0.5f);
    EXPECT_FLOAT_EQ(b.groundDepth, 0.12f);
    EXPECT_FLOAT_EQ(b.style, 1.0f);
    EXPECT_FLOAT_EQ(b.soften, 0.6f);
    EXPECT_FLOAT_EQ(b.creaseWidth, 0.0f);
    EXPECT_EQ(b.blurPasses, 2);
    EXPECT_FLOAT_EQ(b.outlineDepth, 1.5f);
    EXPECT_FLOAT_EQ(b.shading.ambient, 0.55f);
    EXPECT_FLOAT_EQ(b.shading.darkColor[0], 0.1f);
    EXPECT_FLOAT_EQ(b.shading.darkColor[2], 0.3f);
    EXPECT_FLOAT_EQ(b.shading.bottomDarken, 0.4f);
}

TEST(Tech3dAssetData, GameDataFromJson) {
    nlohmann::json j = minimalPayload();
    j["tech3d"] = {
        {"raisedHeight", 48.0},
        {"cellSize", 0.08},
        {"levelHeight", 0.5},
        {"groundDepth", 0.1},
        {"style", 1.0},
        {"soften", 0.4},
        {"creaseWidth", 0.08},
        {"blurPasses", 2},
        {"outlineDepth", 1.0},
        {"shading", {{"diffuse", 0.9}, {"goldColor", {0.7, 0.6, 0.4}}}},
    };
    const game_data::AssetData asset = j.get<game_data::AssetData>();

    EXPECT_EQ(asset.layerType, game_data::LayerType::TechLandscape);
    ASSERT_TRUE(asset.tech3d.has_value());
    EXPECT_FLOAT_EQ(asset.tech3d->raisedHeight, 48.0f);
    EXPECT_FLOAT_EQ(asset.tech3d->cellSize, 0.08f);
    EXPECT_FLOAT_EQ(asset.tech3d->levelHeight, 0.5f);
    EXPECT_FLOAT_EQ(asset.tech3d->groundDepth, 0.1f);
    EXPECT_FLOAT_EQ(asset.tech3d->style, 1.0f);
    EXPECT_FLOAT_EQ(asset.tech3d->soften, 0.4f);
    EXPECT_FLOAT_EQ(asset.tech3d->creaseWidth, 0.08f);
    EXPECT_EQ(asset.tech3d->blurPasses, 2);
    EXPECT_FLOAT_EQ(asset.tech3d->outlineDepth, 1.0f);
    EXPECT_FLOAT_EQ(asset.tech3d->shading.diffuse, 0.9f);
    EXPECT_FLOAT_EQ(asset.tech3d->shading.goldColor[2], 0.4f);
    // Omitted fields keep the generator defaults.
    EXPECT_FLOAT_EQ(asset.tech3d->padding, 0.5f);
    EXPECT_FLOAT_EQ(asset.tech3d->shading.bottomBand, 0.35f);
    // The tech palette retune applies even without an explicit shading block:
    // vein threshold is 2.0 (veins off), not the cliff default 0.8.
    EXPECT_FLOAT_EQ(asset.tech3d->shading.veinThreshold, 2.0f);
}

TEST(Tech3dAssetData, AssetIndexLoadMapsTechEntry) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "tech3d_asset_index_test";
    const std::filesystem::path assetDir = root / "pack" / "TechGrass";
    std::filesystem::create_directories(assetDir);
    {
        std::ofstream file(assetDir / "index.json");
        file << minimalPayload().dump(2);
    }

    const game_data::AssetIndex index = game_data::AssetIndex::load(root);
    const game_data::AssetIndexEntry* entry = index.find(kUuid);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->isTech3d());
    EXPECT_EQ(entry->layerType, game_data::LayerType::TechLandscape);
    EXPECT_FLOAT_EQ(entry->tech.raisedHeight, 96.0f);
    EXPECT_FLOAT_EQ(entry->tech.levelHeight, 0.35f);
    EXPECT_FLOAT_EQ(entry->tech.style, 0.0f);
    EXPECT_FALSE(entry->isSlice());
    EXPECT_FALSE(entry->isShape3d());
    EXPECT_FALSE(entry->isCliff3d());
    EXPECT_FALSE(entry->isStone3d());
    EXPECT_FALSE(entry->isTexture2d());

    std::filesystem::remove_all(root);
}
