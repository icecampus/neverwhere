// Fence authoring ops (MapAuthoring::applyFenceStroke / eraseFenceAt /
// translateFenceAt) — the cell-coordinate fence writes the FencePencil tool
// and the editor RPC fence_stroke/erase_fence ops share. Guards: asset type
// checks, piece fields on the written objects, post/whole-fence erase,
// selection-preserving translate, the get_map dump fields and the save/load
// round-trip of fenceData.
#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <boost/uuid/string_generator.hpp>

#include "assets_library/assets/fence_asset.h"
#include "game_objects/fence.h"
#include "map/map_authoring.h"
#include "map/map_model.h"

namespace
{

std::unique_ptr<FenceAsset> makeFenceAsset(const char* uuidStr, const char* name)
{
    auto asset = std::make_unique<FenceAsset>(nullptr);
    BaseData::AssetData data;
    data.uuid = boost::uuids::string_generator()(uuidStr);
    data.name = name;
    data.layerType = LayerTypes::FenceLandscape;
    data.fence3dData = BaseData::Fence3dAssetData{};
    asset->load(data);
    return asset;
}

constexpr const char* kFenceUuid = "ffffffff-1111-2222-3333-444444444444";

// (kind, axisX, axisY, length) of every Fence object in the layer, in order.
std::vector<std::tuple<int, int, int, int>> fenceObjectsAt(LayerModel& layer, const math::ivec2& cell)
{
    std::vector<std::tuple<int, int, int, int>> out;
    for (GameObject* obj : layer.getObjectsAt(cell))
    {
        if (auto* fence = dynamic_cast<Fence*>(obj))
        {
            out.emplace_back(fence->getKind(), fence->getAxisX(), fence->getAxisY(), fence->getLength());
        }
    }
    return out;
}

int countFenceObjects(LayerModel& layer)
{
    int count = 0;
    layer.iterate([&count](GameObject& obj)
    {
        if (obj.getType() == GameObjectTypes::Fence)
            ++count;
    });
    return count;
}

} // namespace

TEST(FenceAuthoringTest, StrokePlacesPiecesWithFields)
{
    LayerModel layer(nullptr);
    auto fence = makeFenceAsset(kFenceUuid, "fence");

    // Fresh stroke of 4 cells: P S2 P = 3 pieces.
    ASSERT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {2, 2}, {1, 0}, 4), 3);
    EXPECT_EQ(countFenceObjects(layer), 3);

    ASSERT_EQ(fenceObjectsAt(layer, {2, 2}).size(), 1u);
    EXPECT_EQ(fenceObjectsAt(layer, {2, 2})[0], std::make_tuple(0, 0, 0, 1)); // lead post
    ASSERT_EQ(fenceObjectsAt(layer, {3, 2}).size(), 1u);
    EXPECT_EQ(fenceObjectsAt(layer, {3, 2})[0], std::make_tuple(1, 1, 0, 2)); // 2-cell section
    ASSERT_EQ(fenceObjectsAt(layer, {5, 2}).size(), 1u);
    EXPECT_EQ(fenceObjectsAt(layer, {5, 2})[0], std::make_tuple(0, 0, 0, 1)); // tail post

    // Every object carries the fence asset uuid.
    layer.iterate([&fence](GameObject& obj)
    {
        EXPECT_EQ(obj.getAssetUuid(), fence->uuid());
    });

    // Extension from the tail post: S1 P = 2 more pieces.
    ASSERT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {5, 2}, {1, 0}, 2), 2);
    EXPECT_EQ(countFenceObjects(layer), 5);
    EXPECT_EQ(fenceObjectsAt(layer, {6, 2})[0], std::make_tuple(1, 1, 0, 1));
    EXPECT_EQ(fenceObjectsAt(layer, {7, 2})[0], std::make_tuple(0, 0, 0, 1));

    // Too short / blocked plans place nothing.
    EXPECT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {20, 20}, {1, 0}, 2), 0);
    EXPECT_EQ(countFenceObjects(layer), 5);
}

TEST(FenceAuthoringTest, StrokeRejectsNonFenceAsset)
{
    LayerModel layer(nullptr);
    auto fence = makeFenceAsset(kFenceUuid, "fence");

    EXPECT_EQ(MapAuthoring::applyFenceStroke(layer, nullptr, {0, 0}, {1, 0}, 4), 0);
    EXPECT_TRUE(layer.getObjectsAt({0, 0}).empty());
}

TEST(FenceAuthoringTest, ErasePostSplitsFence)
{
    LayerModel layer(nullptr);
    auto fence = makeFenceAsset(kFenceUuid, "fence");

    // P S2 P S2 P along row 4: posts (2,4),(5,4),(8,4).
    ASSERT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {2, 4}, {1, 0}, 7), 5);

    // Erase the middle post with its incident sections -> split in two.
    EXPECT_EQ(MapAuthoring::eraseFenceAt(layer, {5, 4}, false), 3);
    EXPECT_EQ(countFenceObjects(layer), 2);
    EXPECT_TRUE(fenceObjectsAt(layer, {3, 4}).empty());
    EXPECT_TRUE(fenceObjectsAt(layer, {6, 4}).empty());

    // Erasing a section cell (not a post) is a no-op for wholeFence=false.
    ASSERT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {2, 4}, {1, 0}, 6), 3); // rejoin
    EXPECT_EQ(MapAuthoring::eraseFenceAt(layer, {3, 4}, false), 0);
}

TEST(FenceAuthoringTest, EraseWholeFence)
{
    LayerModel layer(nullptr);
    auto fence = makeFenceAsset(kFenceUuid, "fence");

    ASSERT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {2, 2}, {1, 0}, 4), 3);
    ASSERT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {2, 8}, {1, 0}, 4), 3);
    EXPECT_EQ(countFenceObjects(layer), 6);

    // Erasing by any covered cell (a section cell works here) drops the fence.
    EXPECT_EQ(MapAuthoring::eraseFenceAt(layer, {3, 2}, true), 3);
    EXPECT_EQ(countFenceObjects(layer), 3);
    EXPECT_EQ(MapAuthoring::eraseFenceAt(layer, {2, 2}, true), 0); // already gone
}

TEST(FenceAuthoringTest, TranslateMovesObjectsAndKeepsOrder)
{
    LayerModel layer(nullptr);
    auto fence = makeFenceAsset(kFenceUuid, "fence");

    ASSERT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {2, 2}, {1, 0}, 4), 3);
    ASSERT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {2, 6}, {1, 0}, 4), 3);

    // Free shift of the first fence by (0,3) (its cells are free).
    ASSERT_TRUE(MapAuthoring::translateFenceAt(layer, {2, 2}, {0, 3}));
    EXPECT_TRUE(fenceObjectsAt(layer, {2, 2}).empty());
    EXPECT_EQ(fenceObjectsAt(layer, {2, 5}).size(), 1u);
    EXPECT_EQ(fenceObjectsAt(layer, {3, 5})[0], std::make_tuple(1, 1, 0, 2));
    EXPECT_EQ(fenceObjectsAt(layer, {5, 5}).size(), 1u);

    // Shift onto the second fence is rejected.
    EXPECT_FALSE(MapAuthoring::translateFenceAt(layer, {2, 5}, {0, 1}));

    // Zero delta is a no-op.
    EXPECT_FALSE(MapAuthoring::translateFenceAt(layer, {2, 5}, {0, 0}));
}

TEST(FenceAuthoringTest, DumpLayerEmitsFenceFields)
{
    LayerModel layer(nullptr);
    auto fence = makeFenceAsset(kFenceUuid, "fence");
    ASSERT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {2, 2}, {1, 0}, 4), 3);

    const QJsonObject dump = MapAuthoring::dumpLayer(layer);
    const QJsonArray objects = dump["objects"].toArray();
    ASSERT_EQ(objects.size(), 3);
    const QJsonObject section = objects[1].toObject();
    EXPECT_EQ(section["type"].toString(), QStringLiteral("Fence"));
    EXPECT_EQ(section["x"].toInt(), 3);
    EXPECT_EQ(section["y"].toInt(), 2);
    EXPECT_EQ(section["kind"].toInt(), 1);
    EXPECT_EQ(section["axisX"].toInt(), 1);
    EXPECT_EQ(section["axisY"].toInt(), 0);
    EXPECT_EQ(section["length"].toInt(), 2);
    EXPECT_EQ(section["assetUuid"].toString(), fence->uuid().toString(QUuid::WithoutBraces));
}

TEST(FenceAuthoringTest, SaveLoadRoundTripPreservesPieces)
{
    auto fence = makeFenceAsset(kFenceUuid, "fence");

    BaseData::Layer saved;
    {
        LayerModel layer(nullptr);
        ASSERT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {2, 2}, {1, 0}, 7), 5);
        ASSERT_EQ(MapAuthoring::applyFenceStroke(layer, fence.get(), {5, 2}, {0, 1}, 3), 2);
        layer.save(saved);
    }
    ASSERT_EQ(saved.size(), 7u);

    // Serialize through JSON (the map.json path) and load into a fresh layer.
    nlohmann::json j = saved;
    const BaseData::Layer loaded = j.get<BaseData::Layer>();
    ASSERT_EQ(loaded.size(), saved.size());

    LayerModel restored(nullptr);
    restored.load(loaded);
    EXPECT_EQ(countFenceObjects(restored), 7);

    // The rebuilt fence model sees the same structure: one fence, posts at
    // the expected anchors, sections wired geometrically.
    fence_core::FenceModel model = MapAuthoring::buildFenceModel(restored);
    EXPECT_EQ(model.fenceCount(), 1);
    EXPECT_NE(model.pieceAt({2, 2}), nullptr);
    EXPECT_NE(model.pieceAt({8, 2}), nullptr);
    EXPECT_NE(model.pieceAt({5, 4}), nullptr);
    EXPECT_EQ(model.fenceAt({2, 2}), model.fenceAt({5, 4}));
}
