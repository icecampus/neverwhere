"""Resolve the active neverwhere repository root for the debug MCP server."""

from __future__ import annotations

import os
from functools import lru_cache
from pathlib import Path

# Canonical files that mark a neverwhere checkout root.
_REPO_MARKERS = (
    "CMakeLists.txt",
    "AGENTS.md",
)

_ENV_WORKSPACE_KEYS = (
    "WORKSPACE_FOLDER",
)

_ENV_ROOT_KEYS = (
    "NEVERWHERE_REPO_ROOT",
)


def is_neverwhere_repo(path: Path) -> bool:
    root = path.resolve()
    if not root.is_dir():
        return False
    return all((root / marker).is_file() for marker in _REPO_MARKERS)


def _package_fallback_root() -> Path:
    # tools/debug_mcp/repo_root.py -> parents[2] = repo root
    return Path(__file__).resolve().parents[2]


def _candidate_from_env(keys: tuple[str, ...]) -> Path | None:
    for key in keys:
        raw = os.environ.get(key, "").strip()
        if not raw:
            continue
        candidate = Path(raw).expanduser()
        if not candidate.is_absolute():
            candidate = (Path.cwd() / candidate).resolve()
        else:
            candidate = candidate.resolve()
        if is_neverwhere_repo(candidate):
            return candidate
    return None


def _candidate_from_walk(start: Path) -> Path | None:
    here = start.resolve()
    for parent in (here, *here.parents):
        if is_neverwhere_repo(parent):
            return parent
    return None


@lru_cache(maxsize=1)
def repo_root() -> Path:
    """Active neverwhere repo root.

    Resolution order:
    1. Current working directory and its parents (MCP ``cwd`` / active workspace)
    2. WORKSPACE_FOLDER
    3. NEVERWHERE_REPO_ROOT
    4. Location of the installed ``tools.debug_mcp`` package
    """
    from_cwd = _candidate_from_walk(Path.cwd())
    if from_cwd is not None:
        return from_cwd

    from_workspace_env = _candidate_from_env(_ENV_WORKSPACE_KEYS)
    if from_workspace_env is not None:
        return from_workspace_env

    from_root_env = _candidate_from_env(_ENV_ROOT_KEYS)
    if from_root_env is not None:
        return from_root_env

    fallback = _package_fallback_root()
    if is_neverwhere_repo(fallback):
        return fallback.resolve()

    return fallback.resolve()


def reset_repo_root_cache() -> None:
    repo_root.cache_clear()
