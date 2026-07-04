"""Reproduce the 'draw landscape asset on map' crash programmatically.

Before the bounds-check fix in LandNodes::operator[]/at, clicking in the
upper half of the map (where DiamondIsometry::fieldToMap returns negative
Y) caused an access violation because LandNodes::operator[](ivec2(x, -5))
computed a giant index via static_cast<size_t>(-5) * width.

This script drives the editor through RPC: load a chapter, select a
landscape asset + landscape_pencil, then click across the whole screen
(including the top rows). After the fix it prints TEST PASS; if the app
crashes, the socket dies with ConnectionError instead.

Usage:
    1. Launch EpicMapEditor.exe (the RPC server starts automatically).
    2. python -m tools.debug_mcp.smoke_draw_landscape
"""

from __future__ import annotations

import sys
import os
import time

# Repo root on sys.path so `tools.debug_mcp...` resolves.
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
        print(f"chapters: {[ch['name'] for ch in chapters]}")
        target = chapters[0]["name"]
        print(f"load_chapter: {target}")
        c.load_chapter(target)
        # Workspace creation is async in QML; wait for the scene to register.
        if not c.wait_until_map_loaded(timeout_s=8.0):
            print("FAIL: map did not load in time")
            return 1
        print("map loaded")

        assets = c.list_assets()
        landscape_assets = [a for a in assets if a.get("layerType") == "BaseLandscape"]
        if not landscape_assets:
            print("FAIL: no BaseLandscape assets available")
            return 1
        land = landscape_assets[0]
        print(f"select_asset: {land['name']} ({land['uuid']})")
        c.select_asset(land["uuid"])

        print("select_tool: landscape_pencil")
        c.select_tool("landscape_pencil")

        # Sweep across the map, especially the top rows where DiamondIsometry
        # returns negative cell Y coordinates.
        xs = [200, 600, 1000, 1400]
        ys_top = [10, 50, 100, 200]  # upper part of the window — previously crashed
        ys_rest = [400, 700]
        crashed = False
        for y in ys_top + ys_rest:
            for x in xs:
                try:
                    c.click(x, y)
                except ConnectionError as e:
                    print(f"CRASH at click({x},{y}): {e}")
                    crashed = True
                    break
                except EditorRpcError as e:
                    # Some clicks legitimately error out (e.g. no current tool);
                    # the point is to not crash the editor.
                    print(f"  click({x},{y}) -> rpc error: {e}")
                    continue
            if crashed:
                break
            print(f"  row y={y} OK")

        if crashed:
            print("TEST FAIL — editor crashed while drawing landscape")
            return 2

        st = c.status()
        print(f"status after sweep: chapter={st.get('chapter')} map_loaded={st.get('map_loaded')}")
        print("TEST PASS — landscape draw across the map did not crash")
        return 0
    finally:
        c.close()


if __name__ == "__main__":
    raise SystemExit(main())
