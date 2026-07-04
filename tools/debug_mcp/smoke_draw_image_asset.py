"""Reproduce the 'draw an image asset on the map' crash programmatically.

This is a regression guard for the crash that appeared after the diamond
topology migration. Root cause: under Qt 6.11, reading a Q_GADGET value-type
property from QML (e.g. `isoView.dimensions.cellSize.x`) goes through
QQmlValueTypeWrapper::readReference, which invokes qt_static_metacall with
argv[0] == nullptr (Qt regression tracked externally, e.g. MuseScore #33015).
The moc-generated ReadProperty code then dereferences that null pointer and
crashes when a Tile2DView delegate is incubated while drawing with the
`pencil` tool on a GameplayInteractive (image-type) asset.

The workaround exposes scalar mirror properties (cellSizeX / cellSizeY /
cellWidth / cellHeight / aspectRatio) directly on DiamondIsometry, and the
QML delegates read those instead of the gadget-property chain. This script
exercises that path: it loads a chapter, picks a GameplayInteractive asset,
selects the pencil tool and clicks across the screen. After the fix the
editor stays alive; before the fix the first click terminated the process
with exit code 0xC0000005.

Usage:
    1. Launch EpicMapEditor.exe (the RPC server starts automatically).
    2. python -m tools.debug_mcp.smoke_draw_image_asset
"""

from __future__ import annotations

import sys
import os
import time

ROOT = os.environ.get("NEVERWHERE_REPO_ROOT") or os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, ROOT)

from tools.debug_mcp.editor_rpc_client import EditorRpcClient, EditorRpcError  # noqa: E402


def main() -> int:
    c = EditorRpcClient(timeout=15.0)
    try:
        if not c.ping():
            print("FAIL: editor did not respond to ping")
            return 1
        print("ping OK")

        chapters = c.list_chapters()
        if not chapters:
            print("FAIL: no chapters found")
            return 1
        target = chapters[0]["name"]
        print(f"load_chapter: {target}")
        c.load_chapter(target)
        if not c.wait_until_map_loaded(timeout_s=8.0):
            print("FAIL: map did not load in time")
            return 1
        print("map loaded")

        assets = c.list_assets()
        interactive = [a for a in assets if a.get("layerType") == "GameplayInteractive"]
        if not interactive:
            print("FAIL: no GameplayInteractive (image-type) assets")
            return 1
        print(f"image-type assets: {len(interactive)}")

        # Sweep several assets (each triggers a fresh Tile2DView delegate
        # incubation, the exact path that used to crash). Hit a wide range
        # of screen positions, including top rows where DiamondIsometry
        # returns negative cell coordinates.
        sample = interactive[:5]
        points = [(200, 200), (600, 400), (400, 300), (800, 500),
                  (1000, 400), (1500, 300), (300, 200), (500, 500)]
        crashed = False
        for a in sample:
            print(f"select_asset: {a['name']} ({a['uuid']})")
            c.select_asset(a["uuid"])
            print("select_tool: pencil")
            c.select_tool("pencil")
            for (x, y) in points:
                try:
                    c.click(x, y)
                except ConnectionError as e:
                    print(f"CRASH at {a['name']} click({x},{y}): {e}")
                    crashed = True
                    break
                except EditorRpcError as e:
                    print(f"  click({x},{y}) -> rpc error: {e}")
                    continue
            if crashed:
                break
            print(f"  {a['name']} — all clicks OK")

        if crashed:
            print("TEST FAIL — editor crashed while drawing image asset")
            return 2

        st = c.status()
        print(f"status after sweep: chapter={st.get('chapter')} map_loaded={st.get('map_loaded')}")
        print("TEST PASS — image-asset draw across the map did not crash")
        return 0
    finally:
        c.close()


if __name__ == "__main__":
    raise SystemExit(main())
