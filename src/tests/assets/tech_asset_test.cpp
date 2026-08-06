// TechAsset: the live params-editing API behind the editor's tech settings
// panel (techParams/setTechParam) writes the canonical AssetData payload —
// the same store ModelFrameSource::ensureFrameAssets and save() read.
#include <gtest/gtest.h>

#include <QVariantMap>
#include <boost/uuid/string_generator.hpp>

#include "assets_library/assets/tech_asset.h"

namespace {

std::unique_ptr<TechAsset> makeTechAsset() {
    auto asset = std::make_unique<TechAsset>(nullptr);
    BaseData::AssetData data;
    data.uuid = boost::uuids::string_generator()("eeeeeeee-5555-6666-7777-888888888888");
    data.name = "tech";
    data.layerType = LayerTypes::TechLandscape;
    data.tech3dData = BaseData::Tech3dAssetData{};
    asset->load(data);
    return asset;
}

} // namespace

TEST(TechAsset, SetTechParamWritesCanonicalPayload) {
    auto asset = makeTechAsset();
    asset->setTechParam("cellSize", 0.08);
    asset->setTechParam("levelHeight", 0.5);
    asset->setTechParam("style", 1.0);
    asset->setTechParam("soften", 0.4);
    asset->setTechParam("creaseWidth", 0.0);
    asset->setTechParam("blurPasses", 2);
    asset->setTechParam("groundDepth", 0.1);
    asset->setTechParam("outlineDepth", 1.0);
    asset->setTechParam("shading.ambient", 0.55);
    asset->setTechParam("shading.darkColor.1", 0.25);

    // getData() is what ModelFrameSource::ensureFrameAssets reads every sync.
    const BaseData::Tech3dAssetData& d = *asset->getData().tech3dData;
    EXPECT_FLOAT_EQ(d.cellSize, 0.08f);
    EXPECT_FLOAT_EQ(d.levelHeight, 0.5f);
    EXPECT_FLOAT_EQ(d.style, 1.0f);
    EXPECT_FLOAT_EQ(d.soften, 0.4f);
    EXPECT_FLOAT_EQ(d.creaseWidth, 0.0f);
    EXPECT_EQ(d.blurPasses, 2);
    EXPECT_FLOAT_EQ(d.groundDepth, 0.1f);
    EXPECT_FLOAT_EQ(d.outlineDepth, 1.0f);
    EXPECT_FLOAT_EQ(d.shading.ambient, 0.55f);
    EXPECT_FLOAT_EQ(d.shading.darkColor[1], 0.25f);

    // ...and what AssetData::save serializes back to index.json.
    nlohmann::json j;
    BaseData::to_json(j, asset->getData());
    const auto back = j.get<BaseData::AssetData>();
    ASSERT_TRUE(back.tech3dData.has_value());
    EXPECT_FLOAT_EQ(back.tech3dData->cellSize, 0.08f);
    EXPECT_FLOAT_EQ(back.tech3dData->levelHeight, 0.5f);
    EXPECT_FLOAT_EQ(back.tech3dData->style, 1.0f);
    EXPECT_FLOAT_EQ(back.tech3dData->creaseWidth, 0.0f);
    EXPECT_EQ(back.tech3dData->blurPasses, 2);
    EXPECT_FLOAT_EQ(back.tech3dData->outlineDepth, 1.0f);
    EXPECT_FLOAT_EQ(back.tech3dData->shading.darkColor[1], 0.25f);
    EXPECT_FLOAT_EQ(back.tech3dData->shading.veinThreshold, 2.0f); // default survived
}

TEST(TechAsset, TechParamsReadsBackFlatKeys) {
    auto asset = makeTechAsset();
    asset->setTechParam("style", 0.75);

    const QVariantMap params = asset->techParams();
    EXPECT_TRUE(params.contains("style"));
    EXPECT_FLOAT_EQ(params["style"].toFloat(), 0.75f);
    EXPECT_TRUE(params.contains("cellSize"));
    EXPECT_TRUE(params.contains("levelHeight"));
    EXPECT_TRUE(params.contains("soften"));
    EXPECT_TRUE(params.contains("creaseWidth"));
    EXPECT_TRUE(params.contains("shading.goldColor.2"));

    // Unknown keys are ignored silently (typo-safe panel).
    asset->setTechParam("noSuchParam", 42);
    EXPECT_FALSE(asset->techParams().contains("noSuchParam"));
}
