"""Resolve a neverwhere exe (e.g. EpicMapEditor) for debug sessions.

This is the neverwhere counterpart of sandbox's ``debug_desc_exe`` — without
any DESC bits. Build layouts:

- Windows: ``_intermediate_64/{Debug|Release}/{target}.exe``
- macOS:   ``_intermediate_64/src/apps/{target}/{Debug|Release}/{target}``
  (fallbacks: ``src/refs/{target}/...`` and ``src/tests/{config}/{target}``)
"""

from __future__ import annotations

import json
import os
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any

# ---------------------------------------------------------------------------
# Build config policy (Debug default; Release honored when explicitly asked).
# ---------------------------------------------------------------------------

DEFAULT_BUILD_CONFIG = "Debug"
ENV_MCP_BUILD_CONFIG = "NEVERWHERE_MCP_BUILD_CONFIG"
ENV_ALLOW_RELEASE = "NEVERWHERE_MCP_ALLOW_RELEASE"
_VALID_CONFIGS = frozenset({"Debug", "Release"})


def mcp_build_config(requested: str | None = None) -> str:
    if requested and requested.strip() in _VALID_CONFIGS:
        return requested.strip()
    if os.environ.get(ENV_ALLOW_RELEASE, "").strip() == "1":
        env_cfg = os.environ.get(ENV_MCP_BUILD_CONFIG, "").strip()
        if env_cfg in _VALID_CONFIGS:
            return env_cfg
    return DEFAULT_BUILD_CONFIG


# ---------------------------------------------------------------------------
# Env / persistence
# ---------------------------------------------------------------------------

DEFAULT_DEBUG_TARGET = "EpicMapEditor"
ACTIVE_DEBUG_FILE = ".zcode/debug_active.json"
ENV_DEBUG_EXE = "NEVERWHERE_DEBUG_EXE"
ENV_DEBUG_TARGET = "NEVERWHERE_DEBUG_EXE_TARGET"
ENV_DEBUG_CONFIG = "NEVERWHERE_DEBUG_EXE_CONFIG"


@dataclass(frozen=True)
class DebugExeResolution:
    exe: Path
    source: str
    cmake_target: str | None
    config: str
    cwd: Path

    def as_payload(self) -> dict[str, Any]:
        return {
            "path": str(self.exe),
            "source": self.source,
            "cmake_target": self.cmake_target,
            "config": self.config,
            "cwd": str(self.cwd),
        }


def _normalize_path(root: Path, value: str) -> Path:
    p = Path(value).expanduser()
    if not p.is_absolute():
        p = (root / p).resolve()
    return p.resolve()


def product_exe_path(root: Path, target: str, config: str = DEFAULT_BUILD_CONFIG) -> Path:
    """neverwhere cmake layout; platform-specific (see module docstring)."""
    build_config = mcp_build_config(config)
    if sys.platform == "darwin":
        candidates = [
            root / "_intermediate_64" / "src" / "apps" / target / build_config / target,
            root / "_intermediate_64" / "src" / "refs" / target / build_config / target,
            root / "_intermediate_64" / "src" / "tests" / build_config / target,
        ]
        for candidate in candidates:
            if candidate.is_file():
                return candidate
        return candidates[0]
    return root / "_intermediate_64" / build_config / f"{target}.exe"


def _read_active_file(root: Path) -> dict[str, Any] | None:
    path = root / ACTIVE_DEBUG_FILE
    if not path.is_file():
        return None
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    return payload if isinstance(payload, dict) else None


def write_active_debug_config(
    root: Path,
    *,
    cmake_target: str = "",
    exe_path: str = "",
    config: str = DEFAULT_BUILD_CONFIG,
    clear: bool = False,
) -> dict[str, Any]:
    path = root / ACTIVE_DEBUG_FILE
    if clear:
        if path.is_file():
            path.unlink()
        return {"ok": True, "cleared": True, "path": str(path)}

    target = cmake_target.strip()
    explicit = exe_path.strip()
    build_config = mcp_build_config((config or DEFAULT_BUILD_CONFIG).strip() or DEFAULT_BUILD_CONFIG)
    if build_config not in {"Debug", "Release"}:
        return {"ok": False, "error": "config must be Debug or Release"}
    if bool(target) == bool(explicit):
        return {"ok": False, "error": "Provide exactly one of cmake_target or exe_path (or clear=true)"}

    payload: dict[str, Any]
    if explicit:
        payload = {"exe": explicit}
    else:
        payload = {"cmake_target": target, "config": build_config}

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    resolution = resolve_exe(root)
    return {
        "ok": True,
        "path": str(path),
        "written": payload,
        "resolved": resolution.as_payload(),
    }


def default_symbol_paths(root: Path, exe: Path) -> list[str]:
    """Symbol search paths for cdb (``.sympath``).

    For neverwhere the PDBs live next to the exes (in the build dir) and in
    the intermediate build root. On macOS DWARF is embedded in the binary,
    so no symbol paths are needed.
    """
    if sys.platform == "darwin":
        return []
    build_config = mcp_build_config()
    parts = [
        str(exe.parent),
        str(root / "_intermediate_64" / build_config),
    ]
    seen: list[str] = []
    for part in parts:
        norm = str(Path(part).resolve()) if Path(part).exists() else part
        if norm not in seen:
            seen.append(norm)
    return seen


def make_debug_worker_cwd(root: Path, worker_id: str = "debug") -> Path:
    """Scratch cwd for a cdb session — keeps per-session junk out of the repo root."""
    worker_cwd = root / ".zcode" / "debug_workdirs" / f"w{worker_id}"
    worker_cwd.mkdir(parents=True, exist_ok=True)
    return worker_cwd


def _pack_debug_resolution(
    root: Path,
    exe: Path,
    *,
    source: str,
    cmake_target: str | None,
    config: str | None,
) -> DebugExeResolution:
    effective_config = mcp_build_config(config)
    if cmake_target:
        preferred = product_exe_path(root, cmake_target, effective_config)
        if preferred.is_file():
            exe = preferred
    return DebugExeResolution(
        exe=exe,
        source=source,
        cmake_target=cmake_target,
        config=effective_config,
        cwd=exe.parent,
    )


def resolve_exe(
    root: Path,
    *,
    exe_target: str | None = None,
    exe_path: str | None = None,
    config: str | None = None,
) -> DebugExeResolution:
    """Resolve a neverwhere exe with precedence: call args > env > active file > default."""
    param_path = (exe_path or "").strip()
    if param_path:
        exe = _normalize_path(root, param_path)
        return _pack_debug_resolution(
            root, exe, source="param_exe", cmake_target=None, config=config
        )

    param_target = (exe_target or "").strip()
    if param_target:
        return _pack_debug_resolution(
            root,
            product_exe_path(root, param_target, config or DEFAULT_BUILD_CONFIG),
            source="param_target",
            cmake_target=param_target,
            config=config or DEFAULT_BUILD_CONFIG,
        )

    env_exe = os.environ.get(ENV_DEBUG_EXE, "").strip()
    if env_exe:
        exe = _normalize_path(root, env_exe)
        return _pack_debug_resolution(
            root, exe, source="env_exe", cmake_target=None, config=config
        )

    env_target = os.environ.get(ENV_DEBUG_TARGET, "").strip()
    if env_target:
        build_config = (
            os.environ.get(ENV_DEBUG_CONFIG, "").strip()
            or (config or DEFAULT_BUILD_CONFIG).strip()
            or DEFAULT_BUILD_CONFIG
        )
        return _pack_debug_resolution(
            root,
            product_exe_path(root, env_target, build_config),
            source="env_target",
            cmake_target=env_target,
            config=build_config,
        )

    active = _read_active_file(root)
    if active:
        active_exe = str(active.get("exe") or "").strip()
        if active_exe:
            exe = _normalize_path(root, active_exe)
            return _pack_debug_resolution(
                root, exe, source="active_file_exe", cmake_target=None, config=config
            )
        active_target = str(active.get("cmake_target") or "").strip()
        if active_target:
            active_config = (
                str(active.get("config") or DEFAULT_BUILD_CONFIG).strip() or DEFAULT_BUILD_CONFIG
            )
            return _pack_debug_resolution(
                root,
                product_exe_path(root, active_target, active_config),
                source="active_file_target",
                cmake_target=active_target,
                config=active_config,
            )

    return _pack_debug_resolution(
        root,
        product_exe_path(root, DEFAULT_DEBUG_TARGET, DEFAULT_BUILD_CONFIG),
        source="default",
        cmake_target=DEFAULT_DEBUG_TARGET,
        config=DEFAULT_BUILD_CONFIG,
    )


def debug_overrides_payload(root: Path) -> dict[str, Any]:
    active = _read_active_file(root)
    default_exe = product_exe_path(root, DEFAULT_DEBUG_TARGET, DEFAULT_BUILD_CONFIG)
    return {
        "default": default_exe.as_posix(),
        "env": {
            ENV_DEBUG_EXE: os.environ.get(ENV_DEBUG_EXE, "").strip() or None,
            ENV_DEBUG_TARGET: os.environ.get(ENV_DEBUG_TARGET, "").strip() or None,
            ENV_DEBUG_CONFIG: os.environ.get(ENV_DEBUG_CONFIG, "").strip() or None,
        },
        "active_file": str(root / ACTIVE_DEBUG_FILE),
        "active_file_present": bool(active),
        "active_file_payload": active,
        "precedence": [
            "tool exe / exe_target",
            ENV_DEBUG_EXE,
            ENV_DEBUG_TARGET,
            ACTIVE_DEBUG_FILE,
            f"default {DEFAULT_DEBUG_TARGET} ({DEFAULT_BUILD_CONFIG})",
        ],
    }
