// mask3d asset payload: JSON (de)serialization in both domains — BaseData
// (editor) and game_data (runtime AssetIndex). Guards the defaults the
// generator relies on (MaskFieldParams, the sand fallback palette, the
// material strengths).
#include <filesystem>
#include <fstream>

#include <gtest/gtest.h>

#include "base_data/assets.h"
#include "game_data/assets.h"

namespace {

constexpr const char* kUuid = "eeeeeeee-5555-6666-7777-888888888888";

nlohmann::json minimalPayload() {
    return nlohmann::json{
        {"uuid", kUuid},
        {"layerType", "MaskLandscape"},
        {"pivot", {{"x", 0.0}, {"y", 0.0}}},
        {"mask3d", nlohmann::json::object()},
    };
}

} // namespace

TEST(Mask3dAssetData, BaseDataDefaultsWhenPayloadEmpty) {
    const BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();

    EXPECT_EQ(asset.layerType, LayerTypes::MaskLandscape);
    ASSERT_TRUE(asset.mask3dData.has_value());
    const BaseData::Mask3dAssetData& d = *asset.mask3dData;
    // Generator defaults (mask::MaskFieldParams mirror).
    EXPECT_FLOAT_EQ(d.cellSize, 0.04f);
    EXPECT_FLOAT_EQ(d.padding, 0.5f);
    EXPECT_FLOAT_EQ(d.height, 0.2f);
    EXPECT_FLOAT_EQ(d.spreadDistance, 0.0f); // no skirt by default
    EXPECT_FLOAT_EQ(d.sinkFraction, 0.5f);   // half below the water plane
    EXPECT_EQ(d.blurPasses, 0);
    EXPECT_FLOAT_EQ(d.reliefDepth, 0.02f);
    EXPECT_FLOAT_EQ(d.reliefTiling, 1.0f);
    EXPECT_FLOAT_EQ(d.reliefFade, 0.15f);
    EXPECT_FLOAT_EQ(d.raisedHeight, 96.0f);
    // Material: no set by default (the palette look); strengths ready for one.
    EXPECT_TRUE(d.materialSet.empty());
    EXPECT_FLOAT_EQ(d.matTiling, 1.0f);
    EXPECT_FLOAT_EQ(d.matAlbedo, 1.0f);
    EXPECT_FLOAT_EQ(d.matNormal, 1.0f);
    EXPECT_FLOAT_EQ(d.matAo, 1.0f);
    EXPECT_FLOAT_EQ(d.matRough, 0.7f);
    // Sand palette: warm light ramps, no veins, muted spec, flat-lit.
    EXPECT_FLOAT_EQ(d.shading.darkColor[0], 0.55f);
    EXPECT_FLOAT_EQ(d.shading.goldColor[0], 0.85f);
    EXPECT_FLOAT_EQ(d.shading.veinThreshold, 2.0f);
    EXPECT_FLOAT_EQ(d.shading.specStrength, 0.15f);
    EXPECT_FLOAT_EQ(d.shading.bottomDarken, 0.0f);
    EXPECT_FLOAT_EQ(d.shading.grassA[1], 0.76f);
}

TEST(Mask3dAssetData, BaseDataRoundTripPreservesAllFields) {
    BaseData::AssetData asset = minimalPayload().get<BaseData::AssetData>();
    BaseData::Mask3dAssetData& d = *asset.mask3dData;
    d.thumbnail = "thumb.png";
    d.raisedHeight = 64.0f;
    d.cellSize = 0.09f;
    d.padding = 0.7f;
    d.height = 0.4f;
    d.spreadDistance = 2.5f;
    d.sinkFraction = 0.25f;
    d.blurPasses = 2;
    d.reliefDepth = 0.03f;
    d.reliefTiling = 0.5f;
    d.reliefFade = 0.3f;
    d.materialSet = "Ground061";
    d.matTiling = 0.2f;
    d.matAlbedo = 0.8f;
    d.matNormal = 0.6f;
    d.matAo = 0.4f;
    d.matRough = 0.9f;
    d.shading.ambient = 0.55f;
    d.shading.darkColor = {0.1f, 0.2f, 0.3f};
    d.shading.bottomDarken = 0.4f;
    d.shading.underwaterFade = 0.35f;

    nlohmann::json j;
    BaseData::to_json(j, asset);
    const BaseData::AssetData back = j.get<BaseData::AssetData>();

    EXPECT_EQ(back.layerType, LayerTypes::MaskLandscape); // magic_enum name round-trip
    ASSERT_TRUE(back.mask3dData.has_value());
    const BaseData::Mask3dAssetData& b = *back.mask3dData;
    EXPECT_EQ(b.thumbnail, "thumb.png");
    EXPECT_FLOAT_EQ(b.raisedHeight, 64.0f);
    EXPECT_FLOAT_EQ(b.cellSize, 0.09f);
    EXPECT_FLOAT_EQ(b.padding, 0.7f);
    EXPECT_FLOAT_EQ(b.height, 0.4f);
    EXPECT_FLOAT_EQ(b.spreadDistance, 2.5f);
    EXPECT_FLOAT_EQ(b.sinkFraction, 0.25f);
    EXPECT_EQ(b.blurPasses, 2);
    EXPECT_FLOAT_EQ(b.reliefDepth, 0.03f);
    EXPECT_FLOAT_EQ(b.reliefTiling, 0.5f);
    EXPECT_FLOAT_EQ(b.reliefFade, 0.3f);
    EXPECT_EQ(b.materialSet, "Ground061");
    EXPECT_FLOAT_EQ(b.matTiling, 0.2f);
    EXPECT_FLOAT_EQ(b.matAlbedo, 0.8f);
    EXPECT_FLOAT_EQ(b.matNormal, 0.6f);
    EXPECT_FLOAT_EQ(b.matAo, 0.4f);
    EXPECT_FLOAT_EQ(b.matRough, 0.9f);
    EXPECT_FLOAT_EQ(b.shading.ambient, 0.55f);
    EXPECT_FLOAT_EQ(b.shading.darkColor[0], 0.1f);
    EXPECT_FLOAT_EQ(b.shading.darkColor[2], 0.3f);
    EXPECT_FLOAT_EQ(b.shading.bottomDarken, 0.4f);
    EXPECT_FLOAT_EQ(b.shading.underwaterFade, 0.35f);
}

TEST(Mask3dAssetData, GameDataFromJson) {
    nlohmann::json j = minimalPayload();
    j["mask3d"] = {
        {"raisedHeight", 48.0},
        {"cellSize", 0.08},
        {"height", 0.5},
        {"spreadDistance", 1.5},
        {"sinkFraction", 0.75},
        {"blurPasses", 2},
        {"reliefDepth", 0.04},
        {"materialSet", "Ground061"},
        {"matTiling", 0.3},
        {"matAo", 0.5},
        {"shading", {{"diffuse", 0.9}, {"goldColor", {0.7, 0.6, 0.4}}, {"underwaterFade", 0.2}}},
    };
    const game_data::AssetData asset = j.get<game_data::AssetData>();

    EXPECT_EQ(asset.layerType, game_data::LayerType::MaskLandscape);
    ASSERT_TRUE(asset.mask3d.has_value());
    EXPECT_FLOAT_EQ(asset.mask3d->raisedHeight, 48.0f);
    EXPECT_FLOAT_EQ(asset.mask3d->cellSize, 0.08f);
    EXPECT_FLOAT_EQ(asset.mask3d->height, 0.5f);
    EXPECT_FLOAT_EQ(asset.mask3d->spreadDistance, 1.5f);
    EXPECT_FLOAT_EQ(asset.mask3d->sinkFraction, 0.75f);
    EXPECT_EQ(asset.mask3d->blurPasses, 2);
    EXPECT_FLOAT_EQ(asset.mask3d->reliefDepth, 0.04f);
    EXPECT_EQ(asset.mask3d->materialSet, "Ground061");
    EXPECT_FLOAT_EQ(asset.mask3d->matTiling, 0.3f);
    EXPECT_FLOAT_EQ(asset.mask3d->matAo, 0.5f);
    EXPECT_FLOAT_EQ(asset.mask3d->shading.diffuse, 0.9f);
    EXPECT_FLOAT_EQ(asset.mask3d->shading.goldColor[2], 0.4f);
    EXPECT_FLOAT_EQ(asset.mask3d->shading.underwaterFade, 0.2f);
    // Omitted fields keep the generator defaults.
    EXPECT_FLOAT_EQ(asset.mask3d->padding, 0.5f);
    EXPECT_FLOAT_EQ(asset.mask3d->reliefTiling, 1.0f);
    EXPECT_FLOAT_EQ(asset.mask3d->matAlbedo, 1.0f);
    EXPECT_FLOAT_EQ(asset.mask3d->shading.bottomBand, 0.35f);
    // The sand palette retune applies even without an explicit shading block:
    // vein threshold is 2.0 (veins off), not the cliff default 0.8.
    EXPECT_FLOAT_EQ(asset.mask3d->shading.veinThreshold, 2.0f);
}

TEST(Mask3dAssetData, AssetIndexLoadMapsMaskEntry) {
    const std::filesystem::path root =
        std::filesystem::temp_directory_path() / "mask3d_asset_index_test";
    const std::filesystem::path assetDir = root / "pack" / "SandBeach";
    std::filesystem::create_directories(assetDir);
    {
        std::ofstream file(assetDir / "index.json");
        nlohmann::json j = minimalPayload();
        j["mask3d"] = {{"materialSet", "Ground061"}, {"spreadDistance", 1.5}};
        file << j.dump(2);
    }

    const game_data::AssetIndex index = game_data::AssetIndex::load(root);
    const game_data::AssetIndexEntry* entry = index.find(kUuid);
    ASSERT_NE(entry, nullptr);
    EXPECT_TRUE(entry->isMask3d());
    EXPECT_EQ(entry->layerType, game_data::LayerType::MaskLandscape);
    EXPECT_FLOAT_EQ(entry->mask.raisedHeight, 96.0f);
    EXPECT_FLOAT_EQ(entry->mask.height, 0.2f);
    EXPECT_FLOAT_EQ(entry->mask.spreadDistance, 1.5f);
    // The material prefix resolves against the bundle root.
    EXPECT_EQ(entry->materialPrefix, assetDir / "Ground061");
    EXPECT_FALSE(entry->isSlice());
    EXPECT_FALSE(entry->isShape3d());
    EXPECT_FALSE(entry->isCliff3d());
    EXPECT_FALSE(entry->isStone3d());
    EXPECT_FALSE(entry->isTexture2d());
    EXPECT_FALSE(entry->isTech3d());

    std::filesystem::remove_all(root);
}
