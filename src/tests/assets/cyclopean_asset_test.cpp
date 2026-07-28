// CyclopeanAsset: the live params-editing API behind the editor's cyclopean
// settings panel (cyclopeanParams/setCyclopeanParam) writes the canonical
// AssetData payload — the same store ModelFrameSource::ensureFrameAssets and
// save() read.
#include <gtest/gtest.h>

#include <QVariantMap>
#include <boost/uuid/string_generator.hpp>

#include "assets_library/assets/cyclopean_asset.h"

namespace {

std::unique_ptr<CyclopeanAsset> makeCyclopeanAsset() {
    auto asset = std::make_unique<CyclopeanAsset>(nullptr);
    BaseData::AssetData data;
    data.uuid = boost::uuids::string_generator()("ffffffff-1111-2222-3333-444444444444");
    data.name = "cyclopean";
    data.layerType = LayerTypes::CyclopeanLandscape;
    data.cyclopean3dData = BaseData::Cyclopean3dAssetData{};
    asset->load(data);
    return asset;
}

} // namespace

TEST(CyclopeanAsset, SetCyclopeanParamWritesCanonicalPayload) {
    auto asset = makeCyclopeanAsset();
    asset->setCyclopeanParam("raisedHeight", 5.0);
    asset->setCyclopeanParam("rockSeed", 99);
    asset->setCyclopeanParam("rockAmplitude", 0.45);
    asset->setCyclopeanParam("rockEnabled", false);
    asset->setCyclopeanParam("cornerBevel", 0.1);
    asset->setCyclopeanParam("wallSubdivH", 8);
    asset->setCyclopeanParam("wallSubdivV", 10);

    // getData() is what ModelFrameSource::ensureFrameAssets reads every sync.
    const BaseData::Cyclopean3dAssetData& d = *asset->getData().cyclopean3dData;
    EXPECT_FLOAT_EQ(d.raisedHeight, 5.0f);
    EXPECT_EQ(d.rockSeed, 99);
    EXPECT_FLOAT_EQ(d.rockAmplitude, 0.45f);
    EXPECT_FALSE(d.rockEnabled);
    EXPECT_FLOAT_EQ(d.cornerBevel, 0.1f);
    EXPECT_EQ(d.wallSubdivH, 8);
    EXPECT_EQ(d.wallSubdivV, 10);

    // ...and what AssetData::save serializes back to index.json.
    nlohmann::json j;
    BaseData::to_json(j, asset->getData());
    const auto back = j.get<BaseData::AssetData>();
    ASSERT_TRUE(back.cyclopean3dData.has_value());
    EXPECT_FLOAT_EQ(back.cyclopean3dData->raisedHeight, 5.0f);
    EXPECT_EQ(back.cyclopean3dData->rockSeed, 99);
    EXPECT_FALSE(back.cyclopean3dData->rockEnabled);
    EXPECT_EQ(back.cyclopean3dData->wallSubdivV, 10);
    EXPECT_FLOAT_EQ(back.cyclopean3dData->cornerBevel, 0.1f);
}

TEST(CyclopeanAsset, CyclopeanParamsReadsBackFlatKeys) {
    auto asset = makeCyclopeanAsset();
    asset->setCyclopeanParam("raisedHeight", 6.5);

    const QVariantMap params = asset->cyclopeanParams();
    EXPECT_TRUE(params.contains("raisedHeight"));
    EXPECT_FLOAT_EQ(params["raisedHeight"].toFloat(), 6.5f);
    EXPECT_TRUE(params.contains("rockSeed"));
    EXPECT_TRUE(params.contains("rockAmplitude"));
    EXPECT_TRUE(params.contains("rockEnabled"));
    EXPECT_TRUE(params.contains("cornerBevel"));
    EXPECT_TRUE(params.contains("wallSubdivH"));
    EXPECT_TRUE(params.contains("wallSubdivV"));

    // Unknown keys are ignored silently (typo-safe panel).
    asset->setCyclopeanParam("noSuchParam", 42);
    EXPECT_FALSE(asset->cyclopeanParams().contains("noSuchParam"));
}
