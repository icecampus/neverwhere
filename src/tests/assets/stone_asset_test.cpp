// StoneAsset: the live params-editing API behind the editor's stone settings
// panel (stoneParams/setStoneParam) writes the canonical AssetData payload —
// the same store ModelFrameSource::ensureFrameAssets and save() read.
#include <gtest/gtest.h>

#include <QVariantMap>
#include <boost/uuid/string_generator.hpp>

#include "assets_library/assets/stone_asset.h"

namespace {

std::unique_ptr<StoneAsset> makeStoneAsset() {
    auto asset = std::make_unique<StoneAsset>(nullptr);
    BaseData::AssetData data;
    data.uuid = boost::uuids::string_generator()("eeeeeeee-1111-2222-3333-444444444444");
    data.name = "stone";
    data.layerType = LayerTypes::StoneLandscape;
    data.stone3dData = BaseData::Stone3dAssetData{};
    asset->load(data);
    return asset;
}

} // namespace

TEST(StoneAsset, SetStoneParamWritesCanonicalPayload) {
    auto asset = makeStoneAsset();
    asset->setStoneParam("cellSize", 0.06);
    asset->setStoneParam("voroScale", 3.5);
    asset->setStoneParam("grooveDepth", 0.12);
    asset->setStoneParam("flatTop", false);
    asset->setStoneParam("rimWidth", 0.5);
    asset->setStoneParam("shading.ambient", 0.55);
    asset->setStoneParam("shading.darkColor.1", 0.25);
    asset->setStoneParam("grooveAngles.2.0", -1.5);
    asset->setStoneParam("topTexture", "../../../textures/grass.png");
    asset->setStoneParam("grassFade", 0.2);
    asset->setStoneParam("topTexTiles", 4.0);

    // getData() is what ModelFrameSource::ensureFrameAssets reads every sync.
    const BaseData::Stone3dAssetData& d = *asset->getData().stone3dData;
    EXPECT_FLOAT_EQ(d.cellSize, 0.06f);
    EXPECT_FLOAT_EQ(d.voroScale, 3.5f);
    EXPECT_FLOAT_EQ(d.grooveDepth, 0.12f);
    EXPECT_FALSE(d.flatTop);
    EXPECT_FLOAT_EQ(d.rimWidth, 0.5f);
    EXPECT_FLOAT_EQ(d.shading.ambient, 0.55f);
    EXPECT_FLOAT_EQ(d.shading.darkColor[1], 0.25f);
    EXPECT_FLOAT_EQ(d.grooveAngles[2][0], -1.5f);
    EXPECT_EQ(d.topTexture, "../../../textures/grass.png");
    EXPECT_FLOAT_EQ(d.grassFade, 0.2f);
    EXPECT_FLOAT_EQ(d.topTexTiles, 4.0f);

    // ...and what AssetData::save serializes back to index.json.
    nlohmann::json j;
    BaseData::to_json(j, asset->getData());
    const auto back = j.get<BaseData::AssetData>();
    ASSERT_TRUE(back.stone3dData.has_value());
    EXPECT_FLOAT_EQ(back.stone3dData->cellSize, 0.06f);
    EXPECT_FLOAT_EQ(back.stone3dData->voroScale, 3.5f);
    EXPECT_FLOAT_EQ(back.stone3dData->shading.darkColor[1], 0.25f);
    EXPECT_FALSE(back.stone3dData->flatTop);
    EXPECT_EQ(back.stone3dData->topTexture, "../../../textures/grass.png");
    EXPECT_FLOAT_EQ(back.stone3dData->grassFade, 0.2f);
    EXPECT_FLOAT_EQ(back.stone3dData->topTexTiles, 4.0f);
    EXPECT_FLOAT_EQ(back.stone3dData->shading.texScale, 1.0f); // default survived
}

TEST(StoneAsset, StoneParamsReadsBackFlatKeys) {
    auto asset = makeStoneAsset();
    asset->setStoneParam("plateauHeight", 1.5);

    const QVariantMap params = asset->stoneParams();
    EXPECT_TRUE(params.contains("plateauHeight"));
    EXPECT_FLOAT_EQ(params["plateauHeight"].toFloat(), 1.5f);
    EXPECT_TRUE(params.contains("voroScale"));
    EXPECT_TRUE(params.contains("flatTop"));
    EXPECT_TRUE(params.contains("rimBulge"));
    EXPECT_TRUE(params.contains("topTexMix"));
    EXPECT_TRUE(params.contains("shading.goldColor.2"));
    EXPECT_TRUE(params.contains("grooveAngles.1.1"));

    // Unknown keys are ignored silently (typo-safe panel).
    asset->setStoneParam("noSuchParam", 42);
    EXPECT_FALSE(asset->stoneParams().contains("noSuchParam"));
}
