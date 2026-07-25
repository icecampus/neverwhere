// CliffAsset: the live params-editing API behind the editor's cliff settings
// panel (cliffParams/setCliffParam) writes the canonical AssetData payload —
// the same store ModelFrameSource::ensureFrameAssets and save() read.
#include <gtest/gtest.h>

#include <QVariantMap>
#include <boost/uuid/string_generator.hpp>

#include "assets_library/assets/cliff_asset.h"

namespace {

std::unique_ptr<CliffAsset> makeCliffAsset() {
    auto asset = std::make_unique<CliffAsset>(nullptr);
    BaseData::AssetData data;
    data.uuid = boost::uuids::string_generator()("dddddddd-1111-2222-3333-444444444444");
    data.name = "cliff";
    data.layerType = LayerTypes::CliffLandscape;
    data.cliff3dData = BaseData::Cliff3dAssetData{};
    asset->load(data);
    return asset;
}

} // namespace

TEST(CliffAsset, SetCliffParamWritesCanonicalPayload) {
    auto asset = makeCliffAsset();
    asset->setCliffParam("cellSize", 0.06);
    asset->setCliffParam("groundEnabled", true);
    asset->setCliffParam("shading.ambient", 0.55);
    asset->setCliffParam("shading.darkColor.1", 0.25);
    asset->setCliffParam("grooveAngles.2.0", -1.5);

    // getData() is what ModelFrameSource::ensureFrameAssets reads every sync.
    const BaseData::Cliff3dAssetData& d = *asset->getData().cliff3dData;
    EXPECT_FLOAT_EQ(d.cellSize, 0.06f);
    EXPECT_TRUE(d.groundEnabled);
    EXPECT_FLOAT_EQ(d.shading.ambient, 0.55f);
    EXPECT_FLOAT_EQ(d.shading.darkColor[1], 0.25f);
    EXPECT_FLOAT_EQ(d.grooveAngles[2][0], -1.5f);

    // ...and what AssetData::save serializes back to index.json.
    nlohmann::json j;
    BaseData::to_json(j, asset->getData());
    const auto back = j.get<BaseData::AssetData>();
    ASSERT_TRUE(back.cliff3dData.has_value());
    EXPECT_FLOAT_EQ(back.cliff3dData->cellSize, 0.06f);
    EXPECT_FLOAT_EQ(back.cliff3dData->shading.darkColor[1], 0.25f);
    EXPECT_TRUE(back.cliff3dData->groundEnabled);
}

TEST(CliffAsset, CliffParamsReadsBackFlatKeys) {
    auto asset = makeCliffAsset();
    asset->setCliffParam("plateauHeight", 1.5);

    const QVariantMap params = asset->cliffParams();
    EXPECT_TRUE(params.contains("plateauHeight"));
    EXPECT_FLOAT_EQ(params["plateauHeight"].toFloat(), 1.5f);
    EXPECT_TRUE(params.contains("shading.goldColor.2"));
    EXPECT_TRUE(params.contains("grooveAngles.1.1"));
    EXPECT_TRUE(params.contains("groundEnabled"));

    // Unknown keys are ignored silently (typo-safe panel).
    asset->setCliffParam("noSuchParam", 42);
    EXPECT_FALSE(asset->cliffParams().contains("noSuchParam"));
}
