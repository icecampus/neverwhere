#include "pch.h"
#include "map_authoring.h"

#include <QJsonArray>
#include <unordered_set>
#include <magic_enum/magic_enum.hpp>

#include "assets_library/asset.h"
#include "assets_library/assets/slice_asset.h"
#include "game_objects/landscape.h"
#include "game_objects/tile_2d.h"
#include "map/map_model.h"
#include "topology/diamond_isometry.h"
#include "topology/diamond_tiled_landscape.h"

bool MapAuthoring::setTile(LayerModel& layer, const math::ivec2& cell, const Asset* asset)
{
    if (!asset || asset->type != AssetTypes::image)
        return false;

    layer.removeAll(cell);

    auto tile = std::make_unique<Tile2D>();
    tile->setName(asset->name());
    tile->setPosition(cell);
    tile->setAssetUiid(asset->uuid());
    layer.addGameObject(std::move(tile));
    return true;
}

int MapAuthoring::eraseTiles(LayerModel& layer, const math::ivec2& cell)
{
    const int removed = static_cast<int>(layer.getObjectsAt(cell).size());
    layer.removeAll(cell);
    return removed;
}

int MapAuthoring::fillRect(LayerModel& layer, const math::ivec2& from, const math::ivec2& to, const Asset* asset)
{
    if (!asset || asset->type != AssetTypes::image)
        return 0;

    const int minX = std::min(from.x, to.x);
    const int maxX = std::max(from.x, to.x);
    const int minY = std::min(from.y, to.y);
    const int maxY = std::max(from.y, to.y);

    int written = 0;
    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            if (setTile(layer, math::ivec2(x, y), asset))
                ++written;
        }
    }
    return written;
}

int MapAuthoring::applyLandscapeUpdates(LayerModel& layer, SliceAsset* sliceAsset,
    const std::vector<std::pair<math::ivec2, uint8_t>>& updates)
{
    if (!sliceAsset || updates.empty())
        return 0;

    LandNodes landNodes = DiamondTiledLandscape::buildLandNodes(layer);

    // Apply node updates, collecting every cell that touches a changed node.
    // The node map is sparse and unbounded — any coordinate is a valid target.
    std::unordered_set<math::ivec2> dirtyCells;
    for (const auto& [nodePos, value] : updates)
    {
        landNodes[nodePos] = value ? 1 : 0;
        DiamondIsometry::Neighbours cells = DiamondIsometry::nodeNeighboursCell(nodePos);
        dirtyCells.insert(cells.begin(), cells.end());
    }

    // Recompute tile types for the union of affected cells.
    for (const math::ivec2& cellPosition : dirtyCells)
    {
        TileSet::TileType tileType = DiamondTiledLandscape::tileTypeAt(landNodes, cellPosition);
        updateLandscapeCell(&layer, sliceAsset, cellPosition, tileType);
    }

    return static_cast<int>(dirtyCells.size());
}

void MapAuthoring::updateLandscapeCell(LayerModel* layerModel, SliceAsset* sliceAsset,
    const math::ivec2& cellPosition, TileSet::TileType tileType)
{
    // Clean all existing objects in this cell to prevent duplicates and 'ghost' nodes
    layerModel->removeAll(cellPosition);

    if (tileType != TileSet::Unknown)
    {
        std::unique_ptr<Landscape> landObject = std::make_unique<Landscape>(layerModel);
        landObject->setName(QString("Landscape"));
        landObject->setPosition(cellPosition);
        landObject->setAssetUiid(sliceAsset->uuid());
        landObject->setTileIndex(sliceAsset->subTileIndexByType(tileType));
        layerModel->addGameObject(std::move(landObject));
    }
}

QJsonObject MapAuthoring::dumpLayer(LayerModel& layer)
{
    QJsonArray objects;
    layer.iterate([&objects](GameObject& gameObject)
    {
        // Only persisted fields are dumped (name is runtime-only: it is not
        // part of BaseData::GameObject and does not survive save/load), so
        // get_map output can be diffed across save/reload.
        QJsonObject o;
        o["type"] = QString::fromStdString(std::string(magic_enum::enum_name(gameObject.getType())));
        o["x"] = gameObject.getPosition().x;
        o["y"] = gameObject.getPosition().y;
        o["assetUuid"] = gameObject.getAssetUuid().toString(QUuid::WithoutBraces);
        if (const Landscape* landObject = dynamic_cast<const Landscape*>(&gameObject))
        {
            o["tileIndex"] = static_cast<qint64>(landObject->getTileIndex());
        }
        objects.append(o);
    });

    QJsonObject d;
    d["count"] = objects.size();
    d["objects"] = objects;
    return d;
}

QJsonObject MapAuthoring::dumpMap(MapModel& map)
{
    QJsonObject layers;
    for (const LayerTypes::Type layerType : magic_enum::enum_values<LayerTypes::Type>())
    {
        LayerModel* layer = map.layer(layerType);
        if (layer)
        {
            layers[QString::fromStdString(std::string(magic_enum::enum_name(layerType)))] = dumpLayer(*layer);
        }
    }

    QJsonObject d;
    d["layers"] = layers;
    return d;
}
