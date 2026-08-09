// MaskAsset: the live params-editing API behind the editor's mask settings
// panel (maskParams/setMaskParam) writes the canonical AssetData payload —
// the same store ModelFrameSource::ensureFrameAssets and save() read.
#include <gtest/gtest.h>

#include <QVariantMap>
#include <boost/uuid/string_generator.hpp>

#include "assets_library/assets/mask_asset.h"

namespace {

std::unique_ptr<MaskAsset> makeMaskAsset() {
    auto asset = std::make_unique<MaskAsset>(nullptr);
    BaseData::AssetData data;
    data.uuid = boost::uuids::string_generator()("eeeeeeee-5555-6666-7777-888888888888");
    data.name = "mask";
    data.layerType = LayerTypes::MaskLandscape;
    data.mask3dData = BaseData::Mask3dAssetData{};
    asset->load(data);
    return asset;
}

} // namespace

TEST(MaskAsset, SetMaskParamWritesCanonicalPayload) {
    auto asset = makeMaskAsset();
    asset->setMaskParam("cellSize", 0.08);
    asset->setMaskParam("height", 0.5);
    asset->setMaskParam("spreadDistance", 2.0);
    asset->setMaskParam("sinkFraction", 0.25);
    asset->setMaskParam("blurPasses", 2);
    asset->setMaskParam("reliefDepth", 0.03);
    asset->setMaskParam("reliefTiling", 0.5);
    asset->setMaskParam("reliefFade", 0.3);
    asset->setMaskParam("materialSet", "Ground061");
    asset->setMaskParam("matTiling", 0.2);
    asset->setMaskParam("matAlbedo", 0.8);
    asset->setMaskParam("matNormal", 0.6);
    asset->setMaskParam("matAo", 0.4);
    asset->setMaskParam("matRough", 0.9);
    asset->setMaskParam("shading.ambient", 0.55);
    asset->setMaskParam("shading.darkColor.1", 0.25);
    asset->setMaskParam("shading.underwaterFade", 0.35);

    // getData() is what ModelFrameSource::ensureFrameAssets reads every sync.
    const BaseData::Mask3dAssetData& d = *asset->getData().mask3dData;
    EXPECT_FLOAT_EQ(d.cellSize, 0.08f);
    EXPECT_FLOAT_EQ(d.height, 0.5f);
    EXPECT_FLOAT_EQ(d.spreadDistance, 2.0f);
    EXPECT_FLOAT_EQ(d.sinkFraction, 0.25f);
    EXPECT_EQ(d.blurPasses, 2);
    EXPECT_FLOAT_EQ(d.reliefDepth, 0.03f);
    EXPECT_FLOAT_EQ(d.reliefTiling, 0.5f);
    EXPECT_FLOAT_EQ(d.reliefFade, 0.3f);
    EXPECT_EQ(d.materialSet, "Ground061");
    EXPECT_FLOAT_EQ(d.matTiling, 0.2f);
    EXPECT_FLOAT_EQ(d.matAlbedo, 0.8f);
    EXPECT_FLOAT_EQ(d.matNormal, 0.6f);
    EXPECT_FLOAT_EQ(d.matAo, 0.4f);
    EXPECT_FLOAT_EQ(d.matRough, 0.9f);
    EXPECT_FLOAT_EQ(d.shading.ambient, 0.55f);
    EXPECT_FLOAT_EQ(d.shading.darkColor[1], 0.25f);
    EXPECT_FLOAT_EQ(d.shading.underwaterFade, 0.35f);

    // ...and what AssetData::save serializes back to index.json.
    nlohmann::json j;
    BaseData::to_json(j, asset->getData());
    const auto back = j.get<BaseData::AssetData>();
    ASSERT_TRUE(back.mask3dData.has_value());
    EXPECT_FLOAT_EQ(back.mask3dData->cellSize, 0.08f);
    EXPECT_FLOAT_EQ(back.mask3dData->height, 0.5f);
    EXPECT_FLOAT_EQ(back.mask3dData->spreadDistance, 2.0f);
    EXPECT_FLOAT_EQ(back.mask3dData->sinkFraction, 0.25f);
    EXPECT_EQ(back.mask3dData->blurPasses, 2);
    EXPECT_FLOAT_EQ(back.mask3dData->matTiling, 0.2f);
    EXPECT_FLOAT_EQ(back.mask3dData->shading.darkColor[1], 0.25f);
    EXPECT_FLOAT_EQ(back.mask3dData->shading.underwaterFade, 0.35f);
    EXPECT_FLOAT_EQ(back.mask3dData->shading.veinThreshold, 2.0f); // default survived
}

TEST(MaskAsset, MaskParamsReadsBackFlatKeys) {
    auto asset = makeMaskAsset();
    asset->setMaskParam("spreadDistance", 1.5);
    asset->setMaskParam("sinkFraction", 0.75);

    const QVariantMap params = asset->maskParams();
    EXPECT_TRUE(params.contains("spreadDistance"));
    EXPECT_FLOAT_EQ(params["spreadDistance"].toFloat(), 1.5f);
    EXPECT_FLOAT_EQ(params["sinkFraction"].toFloat(), 0.75f);
    EXPECT_TRUE(params.contains("cellSize"));
    EXPECT_TRUE(params.contains("height"));
    EXPECT_TRUE(params.contains("reliefDepth"));
    EXPECT_TRUE(params.contains("matTiling"));
    EXPECT_TRUE(params.contains("matRough"));
    EXPECT_TRUE(params.contains("materialSet"));
    EXPECT_TRUE(params.contains("shading.goldColor.2"));

    // Unknown keys are ignored silently (typo-safe panel).
    asset->setMaskParam("noSuchParam", 42);
    EXPECT_FALSE(asset->maskParams().contains("noSuchParam"));
}
