"""neverwhere editor MCP server — map authoring over the EpicMapEditor RPC.

Stdio transport. Started by ``tools/run_editor_mcp_server.sh`` (macOS) or
``tools/run_editor_mcp_server.ps1`` (Windows) via
``python -m tools.editor_mcp.server``.

Thin proxy to the EpicMapEditor RPC server (raw TCP + line-delimited JSON on
127.0.0.1:9877, see ``src/apps/EpicMapEditor/src/editor_rpc_server.cpp``).
EpicMapEditor must be RUNNING for these tools to work; every response keeps
the RPC envelope (``{"ok": true, "data": ...}`` / ``{"ok": false, "error"}``).
The TCP client itself comes from ``tools.debug_mcp.editor_rpc_client``.
"""

from __future__ import annotations

from typing import Any, Optional

from mcp.server.fastmcp import FastMCP

from tools.debug_mcp.editor_rpc_client import EditorRpcClient, EditorRpcError

_INSTRUCTIONS = """neverwhere editor MCP — author maps in a RUNNING EpicMapEditor.

Typical "make a map from a description" flow (all coordinates are map CELLS,
not screen pixels):

1. ``editor_status`` — confirm the editor is alive (ok=true).
2. ``editor_create_chapter`` (new) or ``editor_load_chapter`` (existing),
   then poll ``editor_status`` until map_loaded=true.
3. ``editor_list_assets`` — pick asset uuids (slice assets = landscape,
   image assets = tiles/objects).
4. Terrain: ``editor_set_landscape`` with bulk node updates [[x, y, 0|1], ...]
   (vertex-centric: raising/lowering a NODE recomputes the touching cells).
5. Tiles: ``editor_fill_rect`` / ``editor_set_tile`` / ``editor_erase_tile``
   (layer: Decoration | BaseLandscape | GameplayInteractive).
6. Verify: ``editor_set_camera`` + ``editor_screenshot`` to LOOK at the map,
   ``editor_get_map`` to diff the content programmatically.
7. ``editor_save``, then ``editor_play`` to play-test the chapter in-editor.
"""

mcp = FastMCP("neverwhere-editor", instructions=_INSTRUCTIONS)

# Lazily-connected persistent TCP client (the MCP stdio server is long-lived).
_client: Optional[EditorRpcClient] = None


def _call(op: str, args: Optional[dict[str, Any]] = None) -> dict:
    """Send one RPC op; reconnect once on transport failure.

    Server-side errors (ok=false) are returned as-is, not retried.
    """
    global _client
    payload: dict[str, Any] = {"op": op}
    if args:
        # Drop unset optional args so the server applies its defaults.
        payload["args"] = {k: v for k, v in args.items() if v is not None}

    last_error: Optional[Exception] = None
    for _ in range(2):
        try:
            if _client is None:
                _client = EditorRpcClient()
            return _client.call(**payload)
        except EditorRpcError as e:
            return {"ok": False, "error": {"kind": e.kind, "message": e.message}}
        except (ConnectionError, OSError) as e:
            last_error = e
            if _client is not None:
                _client.close()
            _client = None
    return {
        "ok": False,
        "error": {
            "kind": "unreachable",
            "message": f"editor RPC unreachable (is EpicMapEditor running?): {last_error}",
        },
    }


@mcp.tool()
def editor_status() -> dict:
    """Editor liveness + current chapter/map/tool state (poll map_loaded)."""
    return _call("status")


@mcp.tool()
def editor_list_chapters() -> dict:
    """List all chapters (name + uuid)."""
    return _call("list_chapters")


@mcp.tool()
def editor_load_chapter(name: str) -> dict:
    """Open a chapter tab by name; poll editor_status until map_loaded=true."""
    return _call("load_chapter", {"name": name})


@mcp.tool()
def editor_create_chapter(name: str) -> dict:
    """Create a brand-new chapter (index.json + empty map on disk) and open it."""
    return _call("create_chapter", {"name": name})


@mcp.tool()
def editor_play(name: str) -> dict:
    """Open (or restart) the in-editor play-test tab for a chapter."""
    return _call("play", {"name": name})


@mcp.tool()
def editor_list_assets() -> dict:
    """List all assets (uuid, name, layerType) from the assets library."""
    return _call("list_assets")


@mcp.tool()
def editor_set_tile(layer: str, x: int, y: int, asset_uuid: str) -> dict:
    """Write one cell (image assets only; idempotent — replaces cell content)."""
    return _call("set_tile", {"layer": layer, "x": x, "y": y, "asset_uuid": asset_uuid})


@mcp.tool()
def editor_erase_tile(layer: str, x: int, y: int) -> dict:
    """Remove every object in one cell of a layer."""
    return _call("erase_tile", {"layer": layer, "x": x, "y": y})


@mcp.tool()
def editor_fill_rect(layer: str, x0: int, y0: int, x1: int, y1: int, asset_uuid: str) -> dict:
    """Fill the inclusive cell rect [x0..x1, y0..y1] with an image asset."""
    return _call(
        "fill_rect",
        {"layer": layer, "x0": x0, "y0": y0, "x1": x1, "y1": y1, "asset_uuid": asset_uuid},
    )


@mcp.tool()
def editor_set_landscape(asset_uuid: str, updates: list[list[int]]) -> dict:
    """Bulk landscape vertex-edit: updates = [[x, y, 0|1], ...] on a slice asset.

    Raises/lowers NODES (not cells); cells touching changed nodes recompute.
    """
    return _call("set_landscape", {"asset_uuid": asset_uuid, "updates": updates})


@mcp.tool()
def editor_get_map(layer: str = "") -> dict:
    """Read back the map content (all layers, or one layer by name)."""
    return _call("get_map", {"layer": layer} if layer else {})


@mcp.tool()
def editor_set_camera(
    x: Optional[float] = None,
    y: Optional[float] = None,
    zoom: Optional[float] = None,
) -> dict:
    """Move the map camera (unset axes keep their current value)."""
    return _call("set_camera", {"x": x, "y": y, "zoom": zoom})


@mcp.tool()
def editor_screenshot(path: str) -> dict:
    """Render the current map view to a PNG file (absolute path recommended)."""
    return _call("screenshot", {"path": path})


@mcp.tool()
def editor_save() -> dict:
    """Save the active chapter's map to disk."""
    return _call("save")


@mcp.tool()
def editor_reload() -> dict:
    """Reload the active chapter's map from disk (discards unsaved edits)."""
    return _call("reload")


if __name__ == "__main__":
    mcp.run()
