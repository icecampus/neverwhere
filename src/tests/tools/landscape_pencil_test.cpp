// LandscapePencil drag-stroke: Begin/Move paint continuously with a per-stroke
// dedup on (node, action), so mouse-move rate doesn't spam recomputes; the
// writes themselves stay idempotent (same content as single clicks).
#include <gtest/gtest.h>

#include <boost/uuid/string_generator.hpp>

#include "assets_library/assets/slice_asset.h"
#include "assets_library/tools/landscape/landscape_pencil.h"
#include "game_objects/landscape.h"
#include "map/map_model.h"
#include "topology/diamond_isometry.h"

namespace {

constexpr const char* kSliceUuid = "00000000-1111-2222-3333-444444444444";

std::unique_ptr<SliceAsset> makeSliceAsset()
{
    auto asset = std::make_unique<SliceAsset>(nullptr);
    BaseData::AssetData data;
    data.uuid = boost::uuids::string_generator()(kSliceUuid);
    data.name = "landscape";
    data.layerType = LayerTypes::BaseLandscape;
    data.sliceData = BaseData::SliceAssetData{};
    asset->load(data);
    return asset;
}

int objectCount(LayerModel& layer)
{
    int total = 0;
    layer.iterate([&total](GameObject&) { ++total; });
    return total;
}

} // namespace

TEST(LandscapePencilStroke, DragPaintsWithDedup)
{
    LayerModel layer(nullptr);
    auto slice = makeSliceAsset();
    DiamondIsometryView iso; // camera (0,0), zoom 1
    LandscapePencil pencil(nullptr);

    // Two screen points over adjacent nodes (node step = (halfW, halfH)).
    const QPoint p1{500, 400};
    const QPoint p2{564, 432};
    const math::ivec2 n1 = iso.screendToNode(math::vec2(500.0f, 400.0f));
    const math::ivec2 n2 = iso.screendToNode(math::vec2(564.0f, 432.0f));
    ASSERT_TRUE(n1 != n2);

    // Begin paints the 4 cells around the node.
    pencil.stroke(StrokeKind::Begin, p1, slice.get(), &layer, &iso, false, false, false);
    EXPECT_EQ(objectCount(layer), 4);

    // Move over the same node is deduped — no recompute, no new content.
    pencil.stroke(StrokeKind::Move, p1, slice.get(), &layer, &iso, false, false, false);
    EXPECT_EQ(objectCount(layer), 4);

    // Move to the next node paints its cells (2 new + 2 shared ones upgraded).
    pencil.stroke(StrokeKind::Move, p2, slice.get(), &layer, &iso, false, false, false);
    EXPECT_EQ(objectCount(layer), 6);

    // Ctrl-Move on the painted node switches action -> erase applies.
    pencil.stroke(StrokeKind::Move, p2, slice.get(), &layer, &iso, true, false, false);
    EXPECT_EQ(objectCount(layer), 4);

    // Same erase Move again is deduped.
    pencil.stroke(StrokeKind::Move, p2, slice.get(), &layer, &iso, true, false, false);
    EXPECT_EQ(objectCount(layer), 4);

    // Erasing the first node too empties the layer (full erase path).
    pencil.stroke(StrokeKind::Move, p1, slice.get(), &layer, &iso, true, false, false);
    EXPECT_EQ(objectCount(layer), 0);

    // Stroke end / new stroke stay functional.
    pencil.stroke(StrokeKind::End, p1, slice.get(), &layer, &iso, false, false, false);
    pencil.stroke(StrokeKind::Begin, p1, slice.get(), &layer, &iso, false, false, false);
    EXPECT_EQ(objectCount(layer), 4);
}
