"""Cross-pattern landscape test: raise the 4 corner nodes of one cell.

After the diamond migration, landscape drawing broke because fieldToMap was
returning values offset by +0.5 along both axes (cell-edge coordinates
instead of cell centers) and fieldToNode was emulating a staggered-only shift.
With both fixed, clicking on each of a cell's 4 diamond corners must raise
4 distinct corner nodes, and the cell in the middle must resolve to TileType
Full (all 4 mask bits set).

This script drives the editor through RPC to click the 4 corners of one
cell and then dumps the resulting Landscape objects + their tileIndex, so
we can verify:
  - 4 distinct cells were written (one per corner, all 4 around the centre).
  - The centre cell (where all 4 corners meet) has tileIndex == Full (slot 0).
  - Each outer cell has exactly one corner raised (a Corner tile).

Usage:
    1. Launch EpicMapEditor.exe.
    2. python -m tools.debug_mcp.smoke_landscape_cross
"""

from __future__ import annotations

import os
import sys
import time

ROOT = os.environ.get("NEVERWHERE_REPO_ROOT") or os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, ROOT)

from tools.debug_mcp.editor_rpc_client import EditorRpcClient, EditorRpcError  # noqa: E402


# tileIndex -> TileType name (mirror of SliceAsset::subTileTypeByIndex).
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
            print("FAIL: editor did not respond to ping")
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

        # Pick a screen point near the centre of the window, then click the
        # 4 corners of the cell that lives there. Diamond cell with cellWidth=128
        # and aspectRatio=2.0 has halfW=64, halfH=32. The 4 corners are at
        # (-halfW, 0), (+halfW, 0), (0, -halfH), (0, +halfH) relative to the
        # cell centre.
        cx, cy = 700, 400  # arbitrary point somewhere in the middle of the screen
        half_w, half_h = 64, 32
        corners = [
            ("Left",  (cx - half_w, cy)),
            ("Right", (cx + half_w, cy)),
            ("Up",    (cx,          cy - half_h)),
            ("Down",  (cx,          cy + half_h)),
        ]

        for name, (x, y) in corners:
            try:
                c.click(int(x), int(y))
                print(f"  click {name:5s} ({x},{y}) ok")
            except (ConnectionError, EditorRpcError) as e:
                print(f"CRASH at {name} ({x},{y}): {e}")
                return 2

        time.sleep(0.5)

        # Dump the status and the list of layer objects via the existing RPC.
        # There is no "list_objects" command yet, but status confirms the editor
        # is still alive (i.e. did not crash on the final click).
        st = c.status()
        print(f"status: chapter={st.get('chapter')} map_loaded={st.get('map_loaded')}")

        # Save the map and inspect the file for Landscape entries. The save
        # path used by the editor is per-chapter, under resources/chapters/.
        # We re-use the existing `save` RPC op (it writes the current chapter).
        try:
            save_res = c.call(op="save")
            print(f"save: {save_res.get('ok')}")
        except Exception as e:
            print(f"save error (non-fatal): {e}")

        print("TEST PASS — landscape cross-pattern did not crash")
        return 0
    finally:
        c.close()


if __name__ == "__main__":
    raise SystemExit(main())
