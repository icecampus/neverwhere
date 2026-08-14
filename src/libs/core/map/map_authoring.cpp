#include "pch.h"
#include "map_authoring.h"

#include <QJsonArray>
#include <unordered_set>
#include <magic_enum/magic_enum.hpp>

#include <fence_core/fence_model.h>

#include "assets_library/asset.h"
#include "assets_library/assets/slice_asset.h"
#include "game_objects/fence.h"
#include "assets_library/assets/building3d_asset.h"
#include "game_objects/landscape.h"
#include "game_objects/tile_2d.h"
#include "game_objects/building.h"
#include "map/building_footprint.h"
#include "map/map_model.h"
#include "topology/diamond_isometry.h"
#include "topology/diamond_tiled_landscape.h"

bool MapAuthoring::setTile(LayerModel& layer, const math::ivec2& cell, const Asset* asset)
{
    if (!asset) return false;

    if (asset->type == AssetTypes::image)
    {
        layer.removeAll(cell);

        auto tile = std::make_unique<Tile2D>();
        tile->setName(asset->name());
        tile->setPosition(cell);
        tile->setAssetUiid(asset->uuid());
        layer.addGameObject(std::move(tile));
        return true;
    }

    if (asset->type == AssetTypes::building3d)
    {
        const auto* buildingAsset = dynamic_cast<const Building3dAsset*>(asset);
        const int fpW = buildingAsset ? buildingAsset->footprintWidth() : 3;
        const int fpH = buildingAsset ? buildingAsset->footprintHeight() : 3;

        std::vector<GameObject*> overlapping;
        layer.iterate([&](GameObject& obj)
        {
            if (obj.getType() != GameObjectTypes::Buildings) return;
            const BaseData::GameObject data = obj.getData();
            const int w = data.buildingData ? data.buildingData->footprintWidth : 1;
            const int h = data.buildingData ? data.buildingData->footprintHeight : 1;
            if (buildingFootprintOverlaps(data.position, w, h, cell, fpW, fpH))
            {
                overlapping.push_back(&obj);
            }
        });
        for (GameObject* obj : overlapping)
        {
            layer.removeGameObject(obj);
        }

        auto building = std::make_unique<Building>();
        building->setName(asset->name());
        building->setPosition(cell);
        building->setAssetUiid(asset->uuid());
        building->setFootprint(fpW, fpH);
        layer.addGameObject(std::move(building));
        return true;
    }

    return false;
}

int MapAuthoring::eraseTiles(LayerModel& layer, const math::ivec2& cell)
{
    const int removed = static_cast<int>(layer.getObjectsAt(cell).size());
    layer.removeAll(cell);
    return removed;
}

int MapAuthoring::eraseBuildingAt(LayerModel& layer, const math::ivec2& cell)
{
    std::vector<GameObject*> hits;
    layer.iterate([&](GameObject& obj)
    {
        if (obj.getType() != GameObjectTypes::Buildings) return;
        const BaseData::GameObject data = obj.getData();
        const int w = data.buildingData ? data.buildingData->footprintWidth : 1;
        const int h = data.buildingData ? data.buildingData->footprintHeight : 1;
        if (buildingFootprintContains(data.position, w, h, cell))
        {
            hits.push_back(&obj);
        }
    });
    for (GameObject* obj : hits)
    {
        layer.removeGameObject(obj);
    }
    return static_cast<int>(hits.size());
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

namespace
{

// Flat piece list of the layer in object order (FenceModel derives its piece
// ids and fenceIds from it deterministically — the tool and the frame builder
// agree on them as long as both rebuild from the same layer).
std::vector<fence_core::FencePieceData> fencePiecesFromLayer(LayerModel& layer)
{
    std::vector<fence_core::FencePieceData> pieces;
    layer.iterate([&pieces](GameObject& obj)
    {
        if (obj.getType() != GameObjectTypes::Fence)
            return;
        const BaseData::GameObject data = obj.getData();
        if (!data.fenceData)
            return;
        fence_core::FencePieceData p;
        p.kind = data.fenceData->kind == 1
            ? fence_core::FencePieceKind::Section
            : fence_core::FencePieceKind::Post;
        p.cell = {data.position.x, data.position.y};
        p.axis = {data.fenceData->axisX, data.fenceData->axisY};
        p.length = data.fenceData->length;
        pieces.push_back(p);
    });
    return pieces;
}

} // namespace

fence_core::FenceModel MapAuthoring::buildFenceModel(LayerModel& layer)
{
    fence_core::FenceModel model;
    model.reset(); // unbounded — editor maps are unbounded
    model.loadPieces(fencePiecesFromLayer(layer));
    return model;
}

int MapAuthoring::applyFenceStroke(LayerModel& layer, const Asset* fenceAsset,
    const math::ivec2& start, const math::ivec2& dir, int cells)
{
    if (!fenceAsset || fenceAsset->type != AssetTypes::fence3d)
        return 0;

    fence_core::FenceModel model = buildFenceModel(layer);
    const fence_core::FenceModel::StrokePlan plan = model.planStroke(start, dir, cells);
    if (!plan.valid)
        return 0;

    for (const fence_core::FenceModel::StrokePiece& sp : plan.pieces)
    {
        auto obj = std::make_unique<Fence>(&layer);
        obj->setName(QString("Fence"));
        obj->setPosition(math::ivec2(sp.cell.x, sp.cell.y));
        obj->setAssetUiid(fenceAsset->uuid());
        obj->setPiece(
            sp.kind == fence_core::FencePieceKind::Section ? 1 : 0,
            sp.axis.x, sp.axis.y, sp.length);
        layer.addGameObject(std::move(obj));
    }
    return static_cast<int>(plan.pieces.size());
}

int MapAuthoring::eraseFenceAt(LayerModel& layer, const math::ivec2& cell, bool wholeFence)
{
    fence_core::FenceModel model = buildFenceModel(layer);
    const fence_core::FencePiece* piece = model.pieceAt(cell);
    if (!piece)
        return 0;

    // Anchor cells of the pieces to delete (a cell holds at most one piece;
    // the piece object sits at its anchor cell).
    std::unordered_set<math::ivec2> anchors;
    if (wholeFence)
    {
        const int fenceId = piece->fenceId;
        for (const fence_core::FencePiece& p : model.pieces())
        {
            if (p.fenceId == fenceId)
                anchors.insert(math::ivec2(p.cell.x, p.cell.y));
        }
    }
    else
    {
        if (piece->kind != fence_core::FencePieceKind::Post)
            return 0; // sections go with their endpoint posts
        const int postId = piece->id;
        anchors.insert(cell);
        for (const fence_core::FencePiece& p : model.pieces())
        {
            if (p.kind == fence_core::FencePieceKind::Section &&
                (p.postA == postId || p.postB == postId))
            {
                anchors.insert(math::ivec2(p.cell.x, p.cell.y));
            }
        }
    }

    int removed = 0;
    for (const math::ivec2& anchor : anchors)
        removed += eraseTiles(layer, anchor);
    return removed;
}

bool MapAuthoring::translateFenceAt(LayerModel& layer, const math::ivec2& cell,
    const math::ivec2& delta)
{
    if (delta == math::ivec2(0, 0))
        return false;

    fence_core::FenceModel model = buildFenceModel(layer);
    const int fenceId = model.fenceAt(cell);
    if (fenceId < 0 || !model.canTranslate(fenceId, delta))
        return false;

    // Move the component's objects in place (setPosition keeps the object
    // order, so derived piece/fence ids — and the tool selection — survive).
    for (const fence_core::FencePiece& p : model.pieces())
    {
        if (p.fenceId != fenceId)
            continue;
        const math::ivec2 anchor(p.cell.x, p.cell.y);
        const std::vector<GameObject*> objects = layer.getObjectsAt(anchor);
        for (GameObject* obj : objects)
        {
            if (obj->getType() == GameObjectTypes::Fence)
                obj->setPosition(anchor + delta);
        }
    }
    return true;
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
        if (const Fence* fenceObject = dynamic_cast<const Fence*>(&gameObject))
        {
            o["kind"] = fenceObject->getKind();
            o["axisX"] = fenceObject->getAxisX();
            o["axisY"] = fenceObject->getAxisY();
            o["length"] = fenceObject->getLength();
        }
        if (gameObject.getType() == GameObjectTypes::Buildings)
        {
            const BaseData::GameObject data = gameObject.getData();
            const int w = data.buildingData ? data.buildingData->footprintWidth : 1;
            const int h = data.buildingData ? data.buildingData->footprintHeight : 1;
            o["footprintWidth"] = w;
            o["footprintHeight"] = h;
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
