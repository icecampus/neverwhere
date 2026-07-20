#!/bin/sh
# macOS launcher for the neverwhere editor MCP server (map authoring over
# the editor RPC on 127.0.0.1:9877 — EpicMapEditor must be running).
#
# Creates a dedicated venv (from a modern Homebrew python3 — NOT the system
# /usr/bin/python3, which is 3.9 and cannot run the MCP SDK), installs the
# requirements once, then execs the stdio MCP server.
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
cd "$REPO_ROOT"

VENV="$REPO_ROOT/tools/editor_mcp/.venv"
VENV_PY="$VENV/bin/python"

_pick_python() {
    # Prefer a modern python3; never the system /usr/bin/python3 (3.9).
    for candidate in \
        /opt/homebrew/bin/python3 \
        /usr/local/bin/python3 \
        "$(command -v python3 || true)"; do
        [ -n "$candidate" ] || continue
        [ -x "$candidate" ] || continue
        case "$candidate" in
            /usr/bin/python3) continue ;;
        esac
        echo "$candidate"
        return 0
    done
    return 1
}

if [ ! -x "$VENV_PY" ]; then
    BASE_PY="$(_pick_python)" || {
        echo "run_editor_mcp_server.sh: no suitable python3 found (need Homebrew python3, not /usr/bin/python3)" >&2
        exit 1
    }
    echo "run_editor_mcp_server.sh: creating venv $VENV with $BASE_PY" >&2
    "$BASE_PY" -m venv "$VENV"
    "$VENV/bin/pip" install -q -r "$REPO_ROOT/tools/editor_mcp/requirements.txt"
elif ! "$VENV_PY" -c "import mcp" >/dev/null 2>&1; then
    # Venv exists but deps are missing/broken — repair.
    "$VENV/bin/pip" install -q -r "$REPO_ROOT/tools/editor_mcp/requirements.txt"
fi

exec "$VENV_PY" -m tools.editor_mcp.server
