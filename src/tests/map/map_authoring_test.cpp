#include <gtest/gtest.h>

#include <QJsonDocument>
#include <boost/uuid/string_generator.hpp>
#include <filesystem>
#include <fstream>

#include "assets_library/assets/image_asset.h"
#include "assets_library/assets/slice_asset.h"
#include "game_objects/landscape.h"
#include "map/map_authoring.h"
#include "map/map_model.h"

// Cell-coordinate authoring ops used by the editor RPC server (automation).
// Guards: idempotent cell writes, rect fills, bulk landscape node updates and
// the JSON dump/save/load round-trip the RPC get_map op relies on.

namespace
{
std::unique_ptr<ImageAsset> makeImageAsset(const char* uuidStr, const char* name)
{
    auto asset = std::make_unique<ImageAsset>(nullptr);
    BaseData::AssetData data;
    data.uuid = boost::uuids::string_generator()(uuidStr);
    data.name = name;
    data.layerType = LayerTypes::Decoration;
    data.imageData = BaseData::ImageAssetData{};
    asset->load(data);
    return asset;
}

std::unique_ptr<SliceAsset> makeSliceAsset(const char* uuidStr, const char* name)
{
    auto asset = std::make_unique<SliceAsset>(nullptr);
    BaseData::AssetData data;
    data.uuid = boost::uuids::string_generator()(uuidStr);
    data.name = name;
    data.layerType = LayerTypes::BaseLandscape;
    data.sliceData = BaseData::SliceAssetData{};
    asset->load(data);
    return asset;
}

constexpr const char* kGrassUuid = "11111111-2222-3333-4444-555555555555";
constexpr const char* kStoneUuid = "aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee";
constexpr const char* kSliceUuid = "00000000-1111-2222-3333-444444444444";
} // namespace

TEST(MapAuthoringTest, SetTileReplacesCellContent)
{
    LayerModel layer(nullptr);
    auto grass = makeImageAsset(kGrassUuid, "grass");
    auto stone = makeImageAsset(kStoneUuid, "stone");

    ASSERT_TRUE(MapAuthoring::setTile(layer, math::ivec2(3, 4), grass.get()));
    ASSERT_TRUE(MapAuthoring::setTile(layer, math::ivec2(3, 4), stone.get()));

    const std::vector<GameObject*> objects = layer.getObjectsAt(math::ivec2(3, 4));
    ASSERT_EQ(objects.size(), 1u);
    EXPECT_EQ(objects[0]->getAssetUuid(), stone->uuid());
}

TEST(MapAuthoringTest, SetTileRejectsSliceAsset)
{
    LayerModel layer(nullptr);
    auto slice = makeSliceAsset(kSliceUuid, "landscape");

    EXPECT_FALSE(MapAuthoring::setTile(layer, math::ivec2(0, 0), slice.get()));
    EXPECT_TRUE(layer.getObjectsAt(math::ivec2(0, 0)).empty());
}

TEST(MapAuthoringTest, EraseTilesRemovesEverything)
{
    LayerModel layer(nullptr);
    auto grass = makeImageAsset(kGrassUuid, "grass");

    ASSERT_TRUE(MapAuthoring::setTile(layer, math::ivec2(1, 1), grass.get()));
    EXPECT_EQ(MapAuthoring::eraseTiles(layer, math::ivec2(1, 1)), 1);
    EXPECT_TRUE(layer.getObjectsAt(math::ivec2(1, 1)).empty());
    EXPECT_EQ(MapAuthoring::eraseTiles(layer, math::ivec2(1, 1)), 0);
}

TEST(MapAuthoringTest, FillRectWritesInclusiveRect)
{
    LayerModel layer(nullptr);
    auto grass = makeImageAsset(kGrassUuid, "grass");

    // Reversed corners must normalize to the same rect.
    EXPECT_EQ(MapAuthoring::fillRect(layer, math::ivec2(3, 4), math::ivec2(1, 2), grass.get()), 9);

    for (int y = 2; y <= 4; ++y)
    {
        for (int x = 1; x <= 3; ++x)
        {
            const std::vector<GameObject*> objects = layer.getObjectsAt(math::ivec2(x, y));
            ASSERT_EQ(objects.size(), 1u) << "cell (" << x << "," << y << ")";
            EXPECT_EQ(objects[0]->getAssetUuid(), grass->uuid());
        }
    }
    EXPECT_TRUE(layer.getObjectsAt(math::ivec2(4, 5)).empty());
}

TEST(MapAuthoringTest, ApplyLandscapeUpdatesRaisesFullCell)
{
    LayerModel layer(nullptr);
    auto slice = makeSliceAsset(kSliceUuid, "landscape");

    // Raise the 4 corner nodes of cell (10, 10) — it becomes a Full tile.
    // The dirty-cell union of those nodes spans (9..11)x(9..11) = 9 cells
    // (a node is shared by 4 cells), all getting corner/line/Full types.
    const std::vector<std::pair<math::ivec2, uint8_t>> updates = {
        {math::ivec2(10, 10), 1}, {math::ivec2(11, 10), 1},
        {math::ivec2(10, 11), 1}, {math::ivec2(11, 11), 1},
    };
    EXPECT_EQ(MapAuthoring::applyLandscapeUpdates(layer, slice.get(), updates), 9);

    const std::vector<GameObject*> center = layer.getObjectsAt(math::ivec2(10, 10));
    ASSERT_EQ(center.size(), 1u);
    const Landscape* land = dynamic_cast<const Landscape*>(center[0]);
    ASSERT_NE(land, nullptr);
    EXPECT_EQ(land->getTileIndex(), slice->subTileIndexByType(TileSet::Full));
    EXPECT_EQ(land->getAssetUuid(), slice->uuid());

    // Idempotency: the same updates again must keep the exact same content.
    EXPECT_EQ(MapAuthoring::applyLandscapeUpdates(layer, slice.get(), updates), 9);
    int total = 0;
    layer.iterate([&total](GameObject&) { ++total; });
    EXPECT_EQ(total, 9);
}

TEST(MapAuthoringTest, ApplyLandscapeUpdatesAcceptsAnyCoordinate)
{
    LayerModel layer(nullptr);
    auto slice = makeSliceAsset(kSliceUuid, "landscape");

    // The node map is sparse and unbounded: negative and far coordinates are
    // all valid painting targets (the 200x200 contour is gone).
    const std::vector<std::pair<math::ivec2, uint8_t>> updates = {
        {math::ivec2(-5, -5), 1}, {math::ivec2(500, 500), 1},
    };
    EXPECT_EQ(MapAuthoring::applyLandscapeUpdates(layer, slice.get(), updates), 8);

    int total = 0;
    layer.iterate([&total](GameObject&) { ++total; });
    EXPECT_EQ(total, 8); // 4 corner tiles per isolated node

    EXPECT_EQ(MapAuthoring::applyLandscapeUpdates(layer, nullptr, updates), 0);
    EXPECT_EQ(MapAuthoring::applyLandscapeUpdates(layer, slice.get(), {}), 0);
}

TEST(MapAuthoringTest, LowerLandscapeNodeRemovesTile)
{
    LayerModel layer(nullptr);
    auto slice = makeSliceAsset(kSliceUuid, "landscape");

    const std::vector<std::pair<math::ivec2, uint8_t>> raise = {
        {math::ivec2(10, 10), 1}, {math::ivec2(11, 10), 1},
        {math::ivec2(10, 11), 1}, {math::ivec2(11, 11), 1},
    };
    MapAuthoring::applyLandscapeUpdates(layer, slice.get(), raise);
    ASSERT_EQ(layer.getObjectsAt(math::ivec2(10, 10)).size(), 1u);

    // Lower one corner — the Full tile degrades to a *Lack type.
    MapAuthoring::applyLandscapeUpdates(layer, slice.get(), {{math::ivec2(11, 11), 0}});
    const std::vector<GameObject*> center = layer.getObjectsAt(math::ivec2(10, 10));
    ASSERT_EQ(center.size(), 1u);
    const Landscape* land = dynamic_cast<const Landscape*>(center[0]);
    ASSERT_NE(land, nullptr);
    EXPECT_EQ(land->getTileIndex(), slice->subTileIndexByType(TileSet::DownLack));
}

TEST(MapAuthoringTest, ReloadReplacesContent)
{
    MapModel map;
    auto grass = makeImageAsset(kGrassUuid, "grass");

    ASSERT_TRUE(MapAuthoring::setTile(*map.layer(LayerTypes::Decoration), math::ivec2(2, 3), grass.get()));

    const std::filesystem::path tmpPath =
        std::filesystem::temp_directory_path() / "map_authoring_reload_test.json";
    const QString path = QString::fromStdString(tmpPath.string());
    map.save(path);

    MapModel loaded;
    loaded.load(path);
    const QJsonObject once = MapAuthoring::dumpMap(loaded);

    // Loading again into the same non-empty model must replace, not append.
    loaded.load(path);
    const QJsonObject twice = MapAuthoring::dumpMap(loaded);

    EXPECT_EQ(QJsonDocument(once).toJson(QJsonDocument::Compact),
              QJsonDocument(twice).toJson(QJsonDocument::Compact));

    int total = 0;
    loaded.layer(LayerTypes::Decoration)->iterate([&total](GameObject&) { ++total; });
    EXPECT_EQ(total, 1);

    std::filesystem::remove(tmpPath);
}

TEST(MapAuthoringTest, RaisedLayerSaveLoadRoundTrip)
{
    MapModel map;
    auto slice = makeSliceAsset(kSliceUuid, "landscape");

    // Raised Landscape objects live on the RaisedLandscape layer (Shape3d
    // assets); the authoring ops are layer-agnostic.
    const std::vector<std::pair<math::ivec2, uint8_t>> updates = {
        {math::ivec2(5, 5), 1}, {math::ivec2(6, 5), 1},
        {math::ivec2(5, 6), 1}, {math::ivec2(6, 6), 1},
    };
    MapAuthoring::applyLandscapeUpdates(*map.layer(LayerTypes::RaisedLandscape), slice.get(), updates);

    const QJsonObject before = MapAuthoring::dumpMap(map);

    const std::filesystem::path tmpPath =
        std::filesystem::temp_directory_path() / "map_authoring_raised_roundtrip_test.json";
    const QString path = QString::fromStdString(tmpPath.string());
    map.save(path);

    MapModel loaded;
    loaded.load(path);
    const QJsonObject after = MapAuthoring::dumpMap(loaded);

    EXPECT_EQ(QJsonDocument(before).toJson(QJsonDocument::Compact),
              QJsonDocument(after).toJson(QJsonDocument::Compact));

    int raisedTotal = 0;
    loaded.layer(LayerTypes::RaisedLandscape)->iterate([&raisedTotal](GameObject&) { ++raisedTotal; });
    EXPECT_GT(raisedTotal, 0);

    std::filesystem::remove(tmpPath);
}

TEST(MapAuthoringTest, CliffLayerSaveLoadRoundTrip)
{
    MapModel map;
    auto slice = makeSliceAsset(kSliceUuid, "landscape");

    // Cliff Landscape objects live on the CliffLandscape layer (Cliff3d
    // assets); the authoring ops are layer-agnostic.
    const std::vector<std::pair<math::ivec2, uint8_t>> updates = {
        {math::ivec2(5, 5), 1}, {math::ivec2(6, 5), 1},
        {math::ivec2(5, 6), 1}, {math::ivec2(6, 6), 1},
    };
    MapAuthoring::applyLandscapeUpdates(*map.layer(LayerTypes::CliffLandscape), slice.get(), updates);

    const QJsonObject before = MapAuthoring::dumpMap(map);

    const std::filesystem::path tmpPath =
        std::filesystem::temp_directory_path() / "map_authoring_cliff_roundtrip_test.json";
    const QString path = QString::fromStdString(tmpPath.string());
    map.save(path);

    MapModel loaded;
    loaded.load(path);
    const QJsonObject after = MapAuthoring::dumpMap(loaded);

    EXPECT_EQ(QJsonDocument(before).toJson(QJsonDocument::Compact),
              QJsonDocument(after).toJson(QJsonDocument::Compact));

    int cliffTotal = 0;
    loaded.layer(LayerTypes::CliffLandscape)->iterate([&cliffTotal](GameObject&) { ++cliffTotal; });
    EXPECT_GT(cliffTotal, 0);

    std::filesystem::remove(tmpPath);
}

TEST(MapAuthoringTest, LoadLegacyMapWithoutCliffLayer)
{
    // Maps saved before the CliffLandscape layer existed have no such key;
    // loading must treat it as empty instead of throwing.
    const std::filesystem::path tmpPath =
        std::filesystem::temp_directory_path() / "map_authoring_legacy_cliff_test.json";
    {
        std::ofstream file(tmpPath);
        file << R"({
            "BaseLandscape": [],
            "Decoration": [],
            "GameplayInteractive": [],
            "RaisedLandscape": []
        })";
    }

    MapModel loaded;
    ASSERT_NO_THROW(loaded.load(QString::fromStdString(tmpPath.string())));

    int cliffTotal = 0;
    loaded.layer(LayerTypes::CliffLandscape)->iterate([&cliffTotal](GameObject&) { ++cliffTotal; });
    EXPECT_EQ(cliffTotal, 0);

    const std::filesystem::path outPath =
        std::filesystem::temp_directory_path() / "map_authoring_legacy_cliff_out_test.json";
    ASSERT_NO_THROW(loaded.save(QString::fromStdString(outPath.string())));

    std::filesystem::remove(tmpPath);
    std::filesystem::remove(outPath);
}

TEST(MapAuthoringTest, LoadLegacyMapWithoutRaisedLayer)
{
    // Maps saved before the RaisedLandscape layer existed have no such key;
    // loading must treat it as empty instead of throwing.
    const std::filesystem::path tmpPath =
        std::filesystem::temp_directory_path() / "map_authoring_legacy_map_test.json";
    {
        std::ofstream file(tmpPath);
        file << R"({
            "BaseLandscape": [],
            "Decoration": [],
            "GameplayInteractive": []
        })";
    }

    MapModel loaded;
    ASSERT_NO_THROW(loaded.load(QString::fromStdString(tmpPath.string())));

    int raisedTotal = 0;
    loaded.layer(LayerTypes::RaisedLandscape)->iterate([&raisedTotal](GameObject&) { ++raisedTotal; });
    EXPECT_EQ(raisedTotal, 0);

    // Saving the loaded map must not throw either (layers addressed via .at()).
    const std::filesystem::path outPath =
        std::filesystem::temp_directory_path() / "map_authoring_legacy_map_out_test.json";
    ASSERT_NO_THROW(loaded.save(QString::fromStdString(outPath.string())));

    std::filesystem::remove(tmpPath);
    std::filesystem::remove(outPath);
}

TEST(MapAuthoringTest, DumpSaveLoadRoundTrip)
{
    MapModel map;
    auto grass = makeImageAsset(kGrassUuid, "grass");
    auto slice = makeSliceAsset(kSliceUuid, "landscape");

    ASSERT_TRUE(MapAuthoring::setTile(*map.layer(LayerTypes::Decoration), math::ivec2(2, 3), grass.get()));
    const std::vector<std::pair<math::ivec2, uint8_t>> updates = {
        {math::ivec2(5, 5), 1}, {math::ivec2(6, 5), 1},
        {math::ivec2(5, 6), 1}, {math::ivec2(6, 6), 1},
    };
    MapAuthoring::applyLandscapeUpdates(*map.layer(LayerTypes::BaseLandscape), slice.get(), updates);

    const QJsonObject before = MapAuthoring::dumpMap(map);

    const std::filesystem::path tmpPath =
        std::filesystem::temp_directory_path() / "map_authoring_roundtrip_test.json";
    map.save(QString::fromStdString(tmpPath.string()));

    MapModel loaded;
    loaded.load(QString::fromStdString(tmpPath.string()));
    const QJsonObject after = MapAuthoring::dumpMap(loaded);

    EXPECT_EQ(QJsonDocument(before).toJson(QJsonDocument::Compact),
              QJsonDocument(after).toJson(QJsonDocument::Compact));

    std::filesystem::remove(tmpPath);
}
