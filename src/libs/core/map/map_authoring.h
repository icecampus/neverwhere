#pragma once

#include <QJsonObject>
#include <utility>
#include <vector>

#include <fence_core/fence_model.h>

#include "base_data/lib.h"
#include "math/lib.h"
#include "topology/topology_common.h"

class Asset;
class SliceAsset;
class LayerModel;
class MapModel;

//MapAuthoring
// Cell-coordinate map editing operations for automation (editor RPC server,
// smoke/unit tests). Unlike Tool subclasses, these write directly into
// LayerModel/MapModel — no screen coordinates, no iso view, no selection
// state. Writes are idempotent: setting a cell replaces its content.
struct MapAuthoring
{
    // Replaces the cell content with a tile built from `asset`.
    // image → Tile2D; building3d → Buildings GameObject occupying the
    // asset footprint (default 3x3, centered on `cell`). Slice (landscape)
    // assets are edited through applyLandscapeUpdates. Returns false on bad input.
    static bool setTile(LayerModel& layer, const math::ivec2& cell, const Asset* asset);

    // Removes every object in the cell. Returns the number of removed objects.
    static int eraseTiles(LayerModel& layer, const math::ivec2& cell);

    // Removes the building3d object whose footprint covers `cell` (origin
    // may sit on a neighbour). Returns the number of removed objects.
    static int eraseBuildingAt(LayerModel& layer, const math::ivec2& cell);

    // Fills the inclusive rect [from..to] with `asset` tiles (setTile each).
    // Returns the number of written cells.
    static int fillRect(LayerModel& layer, const math::ivec2& from, const math::ivec2& to, const Asset* asset);

    // Bulk vertex-edit of the landscape: builds LandNodes once, applies all
    // node updates (x, y, 0/1), then recomputes the union of cells touching
    // the changed nodes. Returns the number of recomputed cells.
    static int applyLandscapeUpdates(LayerModel& layer, SliceAsset* sliceAsset,
        const std::vector<std::pair<math::ivec2, uint8_t>>& updates);

    // Rebuilds one landscape cell from its TileType: clears the cell and
    // inserts a Landscape object (skipped for Unknown). Single implementation
    // shared by LandscapePencil, NoiseGenerator and applyLandscapeUpdates.
    static void updateLandscapeCell(LayerModel* layerModel, SliceAsset* sliceAsset,
        const math::ivec2& cellPosition, TileSet::TileType tileType);

    // Fence authoring (fence3d assets, FenceLandscape layer): the stroke UX of
    // the FencePathPlayground as cell-coordinate operations. The fence graph
    // (endpoint links, components) is derived per call from the layer content
    // via fence_core::FenceModel — the layer stays the single source of truth.
    //
    // Whole-layer fence model (unbounded; piece ids follow the layer object
    // order, so every consumer rebuilding from the same layer — the fence ops,
    // the frame builder, the tool — agrees on piece/fence ids).
    static fence_core::FenceModel buildFenceModel(LayerModel& layer);
    // Plans and places a fence stroke (dir = unit axis, cells = cells to
    // cover; start on a post = extension). Returns placed piece count.
    static int applyFenceStroke(LayerModel& layer, const Asset* fenceAsset,
        const math::ivec2& start, const math::ivec2& dir, int cells);

    // Erases the post at `cell` with its incident sections (wholeFence=false)
    // or the whole fence covering the cell (wholeFence=true). Returns removed
    // piece count.
    static int eraseFenceAt(LayerModel& layer, const math::ivec2& cell, bool wholeFence);

    // Moves the fence covering `cell` by `delta` cells (object positions are
    // edited in place, so piece/fence ids — and any selection — survive).
    static bool translateFenceAt(LayerModel& layer, const math::ivec2& cell,
        const math::ivec2& delta);

    // JSON read-back for automation: layer objects (type, x, y, assetUuid,
    // tileIndex for Landscape — persisted fields only) and a per-layer map
    // dump keyed by LayerTypes enum name.
    static QJsonObject dumpLayer(LayerModel& layer);
    static QJsonObject dumpMap(MapModel& map);
};
