#include <gtest/gtest.h>
#include <cmath>
#include <set>
#include "topology/diamond_isometry.h"
#include "topology/diamond_tiled_landscape.h"

// Round-trip and corner-mapping tests for the diamond projection.
//
// These guard against two regressions that appeared after the staggered →
// diamond migration:
//
//   1. fieldToMap must be the exact inverse of mapToField. The previous
//      formula returned values offset by +0.5 along both axes (cell EDGE
//      coordinates rather than cell CENTERS), so every screen→map conversion
//      (click hit-test, cursor tracking, asset placement) landed in the wrong
//      cell.
//
//   2. fieldToNode must geometrically place a node at the Up-corner of cell
//      (nx, ny) — i.e. the shared vertex of cells (nx,ny), (nx-1,ny),
//      (nx,ny-1), (nx-1,ny-1). The previous implementation shifted the field
//      point by (0, +halfCellHeight) and called fieldToMap, which only worked
//      on the staggered doubled-Y grid and produced unpredictable rounding
//      on the cartesian grid.

namespace
{
constexpr float kTolerance = 1e-4f;

bool ivecEq(const math::ivec2& a, const math::ivec2& b)
{
    return a.x == b.x && a.y == b.y;
}
} // namespace

TEST(DiamondProjectionTest, FieldToMapInvertsMapToFieldAtCellCenters)
{
    DiamondIsometry iso;
    const math::ivec2 cells[] = {
        {0, 0}, {1, 0}, {0, 1}, {1, 1}, {-1, 0}, {0, -1},
        {3, -2}, {-4, 5}, {10, 10}, {-7, -7},
    };

    for (const math::ivec2& cell : cells)
    {
        const math::vec2 world = iso.mapToField(cell);
        const math::ivec2 back = iso.fieldToMap(world);
        EXPECT_TRUE(ivecEq(back, cell))
            << "fieldToMap(mapToField(" << cell.x << "," << cell.y
            << ")) = (" << back.x << "," << back.y << ")";
    }
}

TEST(DiamondProjectionTest, FieldToMapPicksNearestCellCenter)
{
    DiamondIsometry iso;
    const math::vec2 cellSz = iso.dimensions.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    // Cell (2, 3) center, plus a small offset that stays inside the L1 diamond
    // around that center (|dx|/halfW + |dy|/halfH < 1).
    const math::ivec2 cell{2, 3};
    const math::vec2 center = iso.mapToField(cell);

    const float offsets[][2] = {
        { halfW * 0.3f,  0.0f},
        {-halfW * 0.3f,  0.0f},
        { 0.0f,  halfH * 0.3f},
        { 0.0f, -halfH * 0.3f},
        { halfW * 0.2f,  halfH * 0.2f},
        {-halfW * 0.2f, -halfH * 0.2f},
    };

    for (const auto& off : offsets)
    {
        const math::vec2 sample{center.x + off[0], center.y + off[1]};
        const math::ivec2 hit = iso.fieldToMap(sample);
        EXPECT_TRUE(ivecEq(hit, cell))
            << "offset (" << off[0] << "," << off[1] << ") -> cell ("
            << hit.x << "," << hit.y << "), expected (" << cell.x << "," << cell.y << ")";
    }
}

TEST(DiamondProjectionTest, FieldToNodeHitsUpCornerOfOwnCell)
{
    // node (nx, ny) sits at the Up-corner of cell (nx, ny) — one halfH above
    // the cell center. Sampling that corner must round-trip to (nx, ny).
    DiamondIsometry iso;
    const math::ivec2 nodes[] = {
        {0, 0}, {1, 0}, {0, 1}, {1, 1}, {-1, 0}, {0, -1},
        {4, 4}, {-3, 2}, {6, -5},
    };

    for (const math::ivec2& node : nodes)
    {
        const math::vec2 cellCenter = iso.mapToField(node);
        const math::vec2 upCorner{cellCenter.x, cellCenter.y - iso.dimensions.cellSize().y * 0.5f};
        const math::ivec2 hit = iso.fieldToNode(upCorner);
        EXPECT_TRUE(ivecEq(hit, node))
            << "Up-corner of cell (" << node.x << "," << node.y
            << ") resolved to node (" << hit.x << "," << hit.y << ")";
    }
}

TEST(DiamondProjectionTest, FieldToNodeHitsEveryCornerOfCellCorrectly)
{
    // Each geometric corner of a cell's diamond must round-trip to a distinct
    // corner node. We do NOT assert which *named* corner (Left/Up/Right/Down)
    // it maps to here — that's a separate contract between
    // getNeighboursNodeForCell's slot order and the TileSet mask bits, and
    // changing it would also rotate the asset art. We only check the geometric
    // invariant: 4 distinct diamond corners must hit 4 distinct nodes, and
    // each must be a member of getNeighboursNodeForCell(cell).
    DiamondIsometry iso;
    const math::vec2 cellSz = iso.dimensions.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const math::ivec2 cell{5, 5};
    const math::vec2 center = iso.mapToField(cell);

    struct CornerCase
    {
        const char* name;
        math::vec2 worldOffset; // added to the cell center
    };
    const CornerCase cases[] = {
        {"Left",  {-halfW,  0.0f}},
        {"Up",    { 0.0f,  -halfH}},
        {"Right", { halfW,  0.0f}},
        {"Down",  { 0.0f,   halfH}},
    };

    const DiamondTiledLandscape::ModeNeighbours cellCornerNodes =
        DiamondTiledLandscape::getNeighboursNodeForCell(cell);

    std::set<math::ivec2, bool(*)(const math::ivec2&, const math::ivec2&)> seen(
        [](const math::ivec2& a, const math::ivec2& b) {
            return a.x != b.x ? a.x < b.x : a.y < b.y;
        });
    for (const CornerCase& c : cases)
    {
        const math::vec2 sample{center.x + c.worldOffset.x, center.y + c.worldOffset.y};
        const math::ivec2 hit = iso.fieldToNode(sample);

        bool isCornerOfCell = false;
        for (size_t i = 0; i < cellCornerNodes.size(); ++i)
        {
            if (ivecEq(cellCornerNodes[i], hit))
            {
                isCornerOfCell = true;
                break;
            }
        }
        EXPECT_TRUE(isCornerOfCell)
            << c.name << " corner of cell (" << cell.x << "," << cell.y
            << ") resolved to node (" << hit.x << "," << hit.y
            << "), which is NOT one of the cell's 4 corner nodes";

        EXPECT_TRUE(seen.insert(hit).second)
            << c.name << " corner hit the same node as a previous corner";
    }
}

TEST(DiamondProjectionTest, NodeNeighboursCellIsDualOfGetNeighboursNodeForCell)
{
    // For every cell C and every one of its 4 corner nodes N, C must appear
    // in nodeNeighboursCell(N). This is the topological invariant the pencil
    // relies on: raising node N updates exactly the 4 cells that share it.
    const math::ivec2 cells[] = {
        {0, 0}, {1, 1}, {-2, 3}, {5, -4}, {10, 10},
    };

    for (const math::ivec2& cell : cells)
    {
        const DiamondTiledLandscape::ModeNeighbours cellCornerNodes =
            DiamondTiledLandscape::getNeighboursNodeForCell(cell);
        for (size_t i = 0; i < cellCornerNodes.size(); ++i)
        {
            const math::ivec2 node = cellCornerNodes[i];
            const DiamondIsometry::Neighbours neighbours = DiamondIsometry::nodeNeighboursCell(node);
            bool found = false;
            for (size_t j = 0; j < neighbours.size(); ++j)
            {
                if (ivecEq(neighbours[j], cell))
                {
                    found = true;
                    break;
                }
            }
            EXPECT_TRUE(found)
                << "cell (" << cell.x << "," << cell.y
                << ") missing from nodeNeighboursCell(node "
                << node.x << "," << node.y << ")";
        }
    }
}

TEST(DiamondProjectionTest, GetNeighboursNodeForCellSlotsMatchGeometricCorners)
{
    // The contract: getNeighboursNodeForCell returns corner nodes in
    // [Left, Up, Right, Down] slot order, where the slot index matches the
    // mask bit consumed by TileSet (see topology_common.h) and by
    // landscape_core::nodeMaskToTileType.
    //
    // Geometrically a node (nx, ny) sits at the Up-corner of cell (nx, ny),
    // so for cell (cx, cy) the four diamond corners resolve to:
    //   slot 0 Left  -> node (cx,     cy + 1)
    //   slot 1 Up    -> node (cx,     cy    )
    //   slot 2 Right -> node (cx + 1, cy    )
    //   slot 3 Down  -> node (cx + 1, cy + 1)
    //
    // Verify by sampling each slot's node with fieldToNode at the matching
    // geometric corner of the cell's diamond.
    DiamondIsometry iso;
    const math::vec2 cellSz = iso.dimensions.cellSize();
    const float halfW = cellSz.x * 0.5f;
    const float halfH = cellSz.y * 0.5f;

    const math::ivec2 cells[] = {
        {0, 0}, {3, -2}, {-4, 5}, {7, 7},
    };

    struct SlotCorner
    {
        const char* name;
        math::vec2 worldOffset; // relative to the cell's centre
    };
    const SlotCorner slotCorners[4] = {
        {"Left",  {-halfW,  0.0f}},  // slot 0
        {"Up",    { 0.0f,  -halfH}}, // slot 1
        {"Right", { halfW,  0.0f}},  // slot 2
        {"Down",  { 0.0f,   halfH}}, // slot 3
    };

    for (const math::ivec2& cell : cells)
    {
        const DiamondTiledLandscape::ModeNeighbours nodes =
            DiamondTiledLandscape::getNeighboursNodeForCell(cell);
        const math::vec2 center = iso.mapToField(cell);

        for (size_t slot = 0; slot < 4; ++slot)
        {
            const math::vec2 sample{
                center.x + slotCorners[slot].worldOffset.x,
                center.y + slotCorners[slot].worldOffset.y};
            const math::ivec2 resolved = iso.fieldToNode(sample);
            EXPECT_TRUE(ivecEq(resolved, nodes[slot]))
                << "cell (" << cell.x << "," << cell.y << ") slot " << slot
                << " (" << slotCorners[slot].name
                << "): geometric corner resolves to node ("
                << resolved.x << "," << resolved.y << ") but getNeighboursNodeForCell["
                << slot << "] returned node (" << nodes[slot].x << "," << nodes[slot].y << ")";
        }
    }
}
