#!/bin/sh
# macOS/Linux launcher for the Blender MCP server (the official Blender Lab
# `blender-mcp` stdio server, bridging MCP to the Blender add-on socket on
# 127.0.0.1:9876 — Blender must be running with the "MCP" add-on enabled and
# its server started).
#
# Creates a dedicated venv (from a modern python3 >= 3.10 — on macOS NOT the
# system /usr/bin/python3, which is 3.9 and cannot run the MCP SDK), installs
# the requirements once, then execs the stdio MCP server.
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

VENV="$REPO_ROOT/tools/blender_mcp/.venv"
VENV_PY="$VENV/bin/python"

_pick_python() {
    # First python3 >= 3.10 wins. The version check rejects the macOS system
    # /usr/bin/python3 (3.9); on Linux /usr/bin/python3 is usually modern.
    for candidate in \
        /opt/homebrew/bin/python3 \
        /usr/local/bin/python3 \
        "$(command -v python3 || true)" \
        /usr/bin/python3; do
        [ -n "$candidate" ] || continue
        [ -x "$candidate" ] || continue
        "$candidate" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 10) else 1)' 2>/dev/null || continue
        echo "$candidate"
        return 0
    done
    return 1
}

if [ ! -x "$VENV_PY" ]; then
    BASE_PY="$(_pick_python)" || {
        echo "run_blender_mcp_server.sh: no suitable python3 found (need python3 >= 3.10)" >&2
        exit 1
    }
    echo "run_blender_mcp_server.sh: creating venv $VENV with $BASE_PY" >&2
    "$BASE_PY" -m venv "$VENV"
    "$VENV/bin/pip" install -q -r "$REPO_ROOT/tools/blender_mcp/requirements.txt"
elif ! "$VENV_PY" -c "import blmcp" >/dev/null 2>&1; then
    # Venv exists but deps are missing/broken — repair.
    "$VENV/bin/pip" install -q -r "$REPO_ROOT/tools/blender_mcp/requirements.txt"
fi

exec "$VENV/bin/blender-mcp"
