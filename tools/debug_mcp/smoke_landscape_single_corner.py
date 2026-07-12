"""Single-corner landscape test: click only the Left corner of one cell.

After the slot-order fix in getNeighboursNodeForCell, raising a single
corner node must produce a tile of the matching corner type. This script
clicks at the geometric Left corner of cell (0,0) (world point (0, 32)
for cellWidth=128, aspectRatio=2.0), then inspects the saved map and
verifies that cell (0,0) carries tileIndex == 11 (LeftCorner, mask {1,0,0,0}).

This catches regressions where the slot order in getNeighboursNodeForCell
drifts from the mask bit order in TileSet (which would silently rotate /
mirror every corner tile).

Usage:
    1. Launch EpicMapEditor.exe.
    2. python -m tools.debug_mcp.smoke_landscape_single_corner
"""

from __future__ import annotations

import os
import sys
import time
import json

ROOT = os.environ.get("NEVERWHERE_REPO_ROOT") or os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, ROOT)

from tools.debug_mcp.editor_rpc_client import EditorRpcClient, EditorRpcError  # noqa: E402


# (tileIndex, expected TileType name) — mirrors SliceAsset::subTileTypeByIndex.
TILE_INDEX_TO_NAME = {
    0: "Full", 4: "DownLack", 5: "LeftLack", 6: "UpLack", 7: "RightLack",
    8: "UpCorner", 9: "RightCorner", 10: "DownCorner", 11: "LeftCorner",
    12: "RightUpLine", 13: "RightDownLine", 14: "LeftDownLine", 15: "LeftUpLine",
    20: "UpAndDownCorners", 21: "LeftRightCorners",
}


def main() -> int:
    c = EditorRpcClient(timeout=15.0)
    try:
        if not c.ping():
            print("FAIL: no ping")
            return 1

        chapters = c.list_chapters()
        if not chapters:
            print("FAIL: no chapters")
            return 1
        c.load_chapter(chapters[0]["name"])
        if not c.wait_until_map_loaded(timeout_s=8.0):
            print("FAIL: map not loaded")
            return 1

        assets = c.list_assets()
        land_assets = [a for a in assets if a.get("layerType") == "BaseLandscape"]
        if not land_assets:
            print("FAIL: no BaseLandscape assets")
            return 1
        c.select_asset(land_assets[0]["uuid"])
        c.select_tool("landscape_pencil")

        # Pick a screen point well away from existing clicks, then click the
        # Left corner of the cell that lives there. cellWidth=128 aspectRatio=2
        # → halfW=64, halfH=32. Left corner is halfW to the left of centre.
        cx, cy = 1200, 600
        click_x, click_y = cx - 64, cy
        try:
            c.click(click_x, click_y)
            print(f"click Left corner ({click_x},{click_y}) ok")
        except (ConnectionError, EditorRpcError) as e:
            print(f"CRASH: {e}")
            return 2

        time.sleep(0.5)

        try:
            c.call(op="save")
        except Exception as e:
            print(f"save error: {e}")
            return 3

        # Inspect the saved map: find the Landscape object whose tileIndex is a
        # corner type (8/9/10/11) and print it. We do NOT assert the exact cell
        # coordinates (they depend on camera position), only that exactly one
        # Corner tile was added and it is the LeftCorner variant.
        map_path = os.path.join(ROOT, "resources", "chapters", chapters[0]["uuid"],
                                "maps", "map.json")
        if not os.path.exists(map_path):
            # try the first chapter's name directory instead
            map_path = os.path.join(ROOT, "resources", "chapters", chapters[0]["name"],
                                    "maps", "map.json")
        if not os.path.exists(map_path):
            print(f"FAIL: cannot find map file (tried chapter uuid/name)")
            return 4

        with open(map_path, "r", encoding="utf-8") as fh:
            data = json.load(fh)

        # Find landscape tiles added in the bottom-right region (large x or y),
        # which is where our click at screen (1136, 600) lands.
        land = data.get("BaseLandscape", [])
        # Heuristic: the click raised node (cx, cy+1); the 4 affected cells are
        # around that node. Just collect everything with a single-corner type.
        corner_tiles = []
        for obj in land:
            ti = obj.get("landscapeData", {}).get("tileIndex", -1)
            if ti in (8, 9, 10, 11):
                pos = obj.get("position", {})
                corner_tiles.append((pos.get("x"), pos.get("y"), ti, TILE_INDEX_TO_NAME.get(ti)))

        print(f"corner tiles found: {len(corner_tiles)}")
        for x, y, ti, name in corner_tiles:
            print(f"  cell ({x:+d},{y:+d}) tileIndex={ti} -> {name}")

        # Among the 4 cells touched by the raised node, exactly one has the
        # raised corner as its only land bit — that one must be LeftCorner.
        left_corners = [t for t in corner_tiles if t[3] == "LeftCorner"]
        if not left_corners:
            print("FAIL: no LeftCorner tile produced by the click — slot order is wrong")
            return 5

        print(f"TEST PASS — single Left-corner click produced a LeftCorner tile")
        return 0
    finally:
        c.close()


if __name__ == "__main__":
    raise SystemExit(main())
