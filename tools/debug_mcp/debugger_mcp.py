"""Windows native C++ debugger MCP backend (cdb/WinDbg) — session, breakpoints, inspection."""

from __future__ import annotations

import csv
import json
import os
import queue
import re
import shutil
import signal
import struct
import subprocess
import threading
import time
import uuid
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable

from tools.debug_mcp.exe_resolver import (
    debug_overrides_payload,
    default_symbol_paths,
    make_debug_worker_cwd,
    resolve_exe,
    write_active_debug_config,
)
from tools.debug_mcp.repo_root import repo_root

DEBUGGER_MCP_CONTRACT_VERSION = 6
DEBUG_DIAGNOSTIC_SCHEMA_VERSION = 4
DEBUG_AGENT_WORKFLOW_SCHEMA_VERSION = 4
MAX_MEMORY_READ = 4096
MAX_OUTPUT_CHARS = 50_000
DEFAULT_TIMEOUT_MS = 30_000
DEFAULT_STARTUP_TIMEOUT_MS = 30_000
MAX_OPERATION_LOG = 64
MCP_MARKER_PREFIX = "__MCP_MARKER_"
SESSION_REGISTRY_FILE = ".zcode/debugger_sessions.json"

SESSION_STATES = frozenset({"launching", "running", "stopped", "exited", "error"})
BINDING_STATUSES = frozenset({"bound", "pending_deferred", "failed", "ambiguous", "unsupported"})
OPERATION_STATUSES = frozenset({"completed", "timeout", "process_exited", "backend_error"})

_SESSIONS: dict[str, "CdbSession"] = {}
_SESSION_LOCK = threading.Lock()
_REGISTRY_LOCK = threading.Lock()

ERROR_KINDS = frozenset(
    {
        "launch_failed",
        "symbol_not_found",
        "breakpoint_unresolved",
        "process_exited",
        "timeout",
        "backend_error",
        "invalid_expression",
        "session_not_found",
        "invalid_state",
        "invalid_input",
        "dump_not_found",
    }
)

STOP_REASONS = frozenset(
    {
        "breakpoint",
        "exception",
        "step_complete",
        "manual_interrupt",
        "process_exit",
        "initial_stop",
    }
)


@dataclass
class CdbOperationResult:
    status: str
    text: str
    marker_seen: bool = False
    truncated: bool = False
    elapsed_ms: float = 0.0
    command_id: str = ""
    commands: list[str] = field(default_factory=list)
    error_message: str | None = None

    def as_payload(self) -> dict[str, Any]:
        return {
            "status": self.status,
            "marker_seen": self.marker_seen,
            "truncated": self.truncated,
            "elapsed_ms": self.elapsed_ms,
            "command_id": self.command_id,
            "commands": list(self.commands),
            "error_message": self.error_message,
            "raw_preview": _truncate(self.text, 2000)[0],
        }


class CdbOperationTimeout(RuntimeError):
    def __init__(self, operation: CdbOperationResult):
        super().__init__("cdb command exceeded timeout")
        self.operation = operation


def _require_completed(operation: CdbOperationResult) -> str:
    if operation.status == "timeout":
        raise CdbOperationTimeout(operation)
    if operation.status == "process_exited":
        raise RuntimeError("debug session process exited")
    if operation.status == "backend_error":
        raise RuntimeError(operation.error_message or "cdb backend error")
    return operation.text


def _kill_process_tree(pid: int) -> bool:
    if pid <= 0:
        return False
    try:
        result = subprocess.run(
            ["taskkill", "/F", "/T", "/PID", str(pid)],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
        return result.returncode == 0
    except Exception:
        return False


def _session_registry_path() -> Path:
    return repo_root() / SESSION_REGISTRY_FILE


def _read_session_registry() -> dict[str, dict[str, Any]]:
    path = _session_registry_path()
    if not path.is_file():
        return {}
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return {}
    sessions = payload.get("sessions") if isinstance(payload, dict) else None
    if not isinstance(sessions, dict):
        return {}
    out: dict[str, dict[str, Any]] = {}
    for session_id, meta in sessions.items():
        if isinstance(session_id, str) and isinstance(meta, dict):
            out[session_id] = dict(meta)
    return out


def _write_session_registry(entries: dict[str, dict[str, Any]]) -> None:
    path = _session_registry_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "version": DEBUGGER_MCP_CONTRACT_VERSION,
        "updated_at": time.time(),
        "sessions": entries,
    }
    path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def _persist_session_metadata(entry: dict[str, Any]) -> None:
    with _REGISTRY_LOCK:
        entries = _read_session_registry()
        session_id = str(entry.get("session_id") or "").strip()
        if not session_id:
            return
        entries[session_id] = dict(entry)
        _write_session_registry(entries)


def _remove_persisted_session(session_id: str) -> None:
    with _REGISTRY_LOCK:
        entries = _read_session_registry()
        if session_id in entries:
            entries.pop(session_id, None)
            _write_session_registry(entries)


def _process_exists(pid: int | None) -> bool:
    if not pid or pid <= 0:
        return False
    try:
        result = subprocess.run(
            ["tasklist", "/FI", f"PID eq {pid}", "/FO", "CSV", "/NH"],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
    except Exception:
        return False
    return str(pid) in (result.stdout or "")


def _registry_snapshot_entry(meta: dict[str, Any]) -> dict[str, Any]:
    entry = dict(meta)
    cdb_pid = int(entry.get("cdb_pid") or 0) or None
    inferior_pid = int(entry.get("inferior_pid") or 0) or None
    entry["cdb_alive"] = _process_exists(cdb_pid)
    entry["inferior_alive"] = _process_exists(inferior_pid)
    entry["orphaned"] = True
    entry["active_in_process"] = False
    return entry


def _cleanup_registry_entry(meta: dict[str, Any], *, force: bool) -> dict[str, Any]:
    cdb_pid = int(meta.get("cdb_pid") or 0) or None
    inferior_pid = int(meta.get("inferior_pid") or 0) or None
    attached = bool(meta.get("attached"))
    cleanup_actions: list[str] = []
    if cdb_pid and _kill_process_tree(cdb_pid):
        cleanup_actions.append(f"cdb_tree_kill:{cdb_pid}")
    if force and inferior_pid and (not attached or bool(meta.get("kill_attached_inferior"))):
        if _kill_process_tree(inferior_pid):
            cleanup_actions.append(f"inferior_tree_kill:{inferior_pid}")
    return {
        "session_id": meta.get("session_id"),
        "cleanup_actions": cleanup_actions,
        "attached": attached,
        "cdb_pid": cdb_pid,
        "inferior_pid": inferior_pid,
        "orphaned": True,
    }


def _vcpkg_root() -> Path:
    env_root = os.environ.get("VCPKG_ROOT", "").strip()
    if env_root:
        return Path(env_root)
    return repo_root() / "toolchain" / "vcpkg"


_VCPKG_CDB_TRIPLETS = (
    "x64-windows",
    "x64-windows-static",
    "x64-windows-static-md",
)


def _vcpkg_cdb_path() -> str | None:
    search_roots: list[Path] = []
    manifest_installed = repo_root() / "_b-win64_18" / "vcpkg_installed"
    if manifest_installed.is_dir():
        search_roots.append(manifest_installed)
    root = _vcpkg_root()
    search_roots.append(root / "installed")
    for installed_root in search_roots:
        for triplet in _VCPKG_CDB_TRIPLETS:
            path = installed_root / triplet / "tools" / "debuggers" / "x64" / "cdb.exe"
            if path.is_file():
                return str(path.resolve())
    return None


def find_cdb(custom_path: str | None = None) -> dict[str, Any]:
    candidates = [
        ("env", os.environ.get("CRASH_ANALYSIS_DEBUGGER_PATH")),
        ("vcpkg", _vcpkg_cdb_path()),
    ]
    if custom_path and os.path.isfile(custom_path):
        return {"path": os.path.abspath(custom_path), "source": "custom", "found": True}
    for source, path in candidates:
        if path and os.path.isfile(path):
            return {"path": os.path.abspath(path), "source": source, "found": True}
    return {"path": None, "source": None, "found": False}


def _error(
    kind: str,
    message: str,
    *,
    recoverable: bool = False,
    suggested_next_actions: list[str] | None = None,
) -> dict[str, Any]:
    return {
        "kind": kind,
        "message": message,
        "recoverable": recoverable,
        "suggested_next_actions": suggested_next_actions or [],
    }


def _envelope(
    *,
    ok: bool,
    session_id: str | None,
    state: str | None,
    summary: str,
    data: dict[str, Any] | None = None,
    error: dict[str, Any] | None = None,
    diagnostics: list[str] | None = None,
) -> dict[str, Any]:
    return {
        "ok": ok,
        "debugger_mcp_contract_version": DEBUGGER_MCP_CONTRACT_VERSION,
        "session_id": session_id,
        "state": state,
        "summary": summary,
        "data": data or {},
        "error": error,
        "diagnostics": diagnostics or [],
        "backend": "cdb",
    }


def _truncate(text: str, limit: int = MAX_OUTPUT_CHARS) -> tuple[str, bool]:
    if len(text) <= limit:
        return text, False
    return text[:limit] + "\n...[truncated]", True


def _cdb_path(path: str | Path) -> str:
    """cdb treats backslash sequences in commands as escapes (e.g. \\b in _b-win64)."""
    return str(path).replace("\\", "/")


def _resolve_exe(exe: str, cwd: str | None) -> Path:
    path = Path(exe)
    if path.is_file():
        return path.resolve()
    base = Path(cwd) if cwd else repo_root()
    candidate = (base / exe).resolve()
    if candidate.is_file():
        return candidate
    product = (repo_root() / "_product" / exe).resolve()
    if product.is_file():
        return product
    raise FileNotFoundError(f"Executable not found: {exe}")


def _normalize_source_path(path: str | Path) -> str:
    return str(path).replace("\\", "/").casefold()


def _normalize_process_name(name: str) -> str:
    normalized = name.strip().casefold()
    if normalized.endswith(".exe"):
        normalized = normalized[:-4]
    return normalized


def _list_processes() -> list[dict[str, Any]]:
    proc = subprocess.run(
        ["tasklist", "/fo", "csv", "/nh"],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=30,
        check=False,
    )
    output = proc.stdout or ""
    if proc.returncode != 0:
        raise RuntimeError(f"tasklist failed: {_truncate(output, 2000)[0]}")
    rows = csv.reader(output.splitlines())
    result: list[dict[str, Any]] = []
    for row in rows:
        if len(row) < 5:
            continue
        image_name = row[0].strip()
        try:
            pid = int(row[1].replace(",", "").strip())
        except ValueError:
            continue
        result.append(
            {
                "process_name": image_name,
                "process_name_normalized": _normalize_process_name(image_name),
                "process_id": pid,
                "session_name": row[2].strip(),
                "session_num": row[3].strip(),
                "mem_usage": row[4].strip(),
            }
        )
    return result


def _score_process_match(query: str, process_name: str) -> int:
    q = _normalize_process_name(query)
    p = _normalize_process_name(process_name)
    if not q or not p:
        return 0
    if p == q:
        return 400
    if p.startswith(q):
        return 300
    if q in p:
        return 200
    tokens = [token for token in re.split(r"[^a-z0-9]+", p) if token]
    if any(token.startswith(q) for token in tokens):
        return 180
    if any(q in token for token in tokens):
        return 160
    return 0


def find_processes_by_name(name_query: str, *, limit: int = 20) -> list[dict[str, Any]]:
    query = name_query.strip()
    if not query:
        return []
    matches: list[dict[str, Any]] = []
    for item in _list_processes():
        score = _score_process_match(query, item["process_name"])
        if score <= 0:
            continue
        matches.append({**item, "match_score": score})
    matches.sort(key=lambda item: (-int(item["match_score"]), item["process_name_normalized"], int(item["process_id"])))
    return matches[: max(1, limit)]


def _resolve_process_attach_target(*, process_id: int = 0, process_name: str = "") -> tuple[int | None, dict[str, Any] | None]:
    if process_id > 0 and process_name.strip():
        return None, {
            "kind": "invalid_input",
            "message": "process_id cannot be combined with process_name",
            "recoverable": True,
            "suggested_next_actions": [
                "Pass only process_id for direct attach.",
                "Or pass only process_name to resolve by name.",
            ],
        }
    if process_id > 0:
        return process_id, None
    query = process_name.strip()
    if not query:
        return None, None
    matches = find_processes_by_name(query)
    if not matches:
        return None, {
            "kind": "invalid_input",
            "message": f"No running process matched '{query}'",
            "recoverable": True,
            "suggested_next_actions": [
                "Call debug_process_find with a broader name fragment.",
                "Verify that the target process is running.",
            ],
            "candidates": [],
        }
    top_score = int(matches[0]["match_score"])
    top_matches = [item for item in matches if int(item["match_score"]) == top_score]
    exact_matches = [
        item
        for item in top_matches
        if _normalize_process_name(item["process_name"]) == _normalize_process_name(query)
    ]
    chosen = exact_matches[0] if len(exact_matches) == 1 else (top_matches[0] if len(top_matches) == 1 else None)
    if chosen is None:
        return None, {
            "kind": "invalid_input",
            "message": f"Process name '{query}' is ambiguous",
            "recoverable": True,
            "suggested_next_actions": [
                "Pass process_id explicitly.",
                "Call debug_process_find to inspect matching processes.",
            ],
            "candidates": top_matches[:10],
        }
    return int(chosen["process_id"]), None


def find_llvm_pdbutil() -> str | None:
    explicit = os.environ.get("LLVM_PDBUTIL_PATH", "").strip()
    if explicit and os.path.isfile(explicit):
        return explicit
    candidates = [
        shutil.which("llvm-pdbutil.exe"),
        shutil.which("llvm-pdbutil"),
        r"C:\Program Files\LLVM\bin\llvm-pdbutil.exe",
    ]
    for candidate in candidates:
        if candidate and os.path.isfile(candidate):
            return candidate
    return None


def _run_pdbutil(args: list[str], *, timeout_s: int = 120) -> str:
    tool = find_llvm_pdbutil()
    if not tool:
        raise RuntimeError("llvm-pdbutil.exe not found")
    proc = subprocess.run(
        [tool, *args],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=timeout_s,
        check=False,
    )
    output = proc.stdout or ""
    if proc.returncode != 0:
        raise RuntimeError(f"llvm-pdbutil failed: {_truncate(output, 2000)[0]}")
    return output


def _parse_pdb_modules_for_source(modules_text: str, source_file: str) -> list[int]:
    target = _normalize_source_path(source_file)
    module_ids: set[int] = set()
    current_mod: int | None = None
    lines: list[str] = []
    for raw_line in modules_text.splitlines():
        if raw_line.startswith("\\") and lines:
            lines[-1] += raw_line.rstrip()
        else:
            lines.append(raw_line.rstrip())
    for line in lines:
        mod_match = re.match(r"^\s*>?\s*Mod\s+(\d+)\s+\|", line)
        if mod_match:
            current_mod = int(mod_match.group(1))
            continue
        if current_mod is None:
            continue
        normalized_line = _normalize_source_path(line)
        if target in normalized_line:
            module_ids.add(current_mod)
    return sorted(module_ids)


def _parse_pdb_line_records(lines_text: str, source_file: str, target_line: int) -> list[tuple[int, int]]:
    normalized_target = _normalize_source_path(source_file)
    matches: list[tuple[int, int]] = []
    current_file_matches = False
    current_section: int | None = None
    logical_lines: list[str] = []
    for raw_line in lines_text.splitlines():
        if raw_line.startswith("\\") and logical_lines:
            logical_lines[-1] += raw_line.rstrip()
        else:
            logical_lines.append(raw_line.rstrip())
    for line in logical_lines:
        if not line.strip():
            continue
        if line.lstrip().startswith("E:") or ":/" in line or ":\\" in line:
            current_file_matches = normalized_target in _normalize_source_path(line)
            current_section = None
            continue
        section_match = re.match(r"^\s*([0-9A-Fa-f]{4}):([0-9A-Fa-f]{8})-", line)
        if section_match:
            current_section = int(section_match.group(1), 16)
            continue
        if not current_file_matches or current_section is None:
            continue
        for line_no, offset_hex in re.findall(r"(\d+)\s+([0-9A-Fa-f]{8})", line):
            if int(line_no) == target_line:
                matches.append((current_section, int(offset_hex, 16)))
    return matches


def _parse_pe_sections(exe_path: Path) -> dict[int, int]:
    data = exe_path.read_bytes()
    if len(data) < 0x40:
        raise RuntimeError(f"PE image too small: {exe_path}")
    pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe_offset : pe_offset + 4] != b"PE\x00\x00":
        raise RuntimeError(f"Invalid PE signature: {exe_path}")
    coff_offset = pe_offset + 4
    number_of_sections = struct.unpack_from("<H", data, coff_offset + 2)[0]
    size_of_optional_header = struct.unpack_from("<H", data, coff_offset + 16)[0]
    section_table = coff_offset + 20 + size_of_optional_header
    sections: dict[int, int] = {}
    for index in range(number_of_sections):
        base = section_table + index * 40
        virtual_address = struct.unpack_from("<I", data, base + 12)[0]
        sections[index + 1] = virtual_address
    return sections


def _derive_cdb_module_name(exe_path: Path) -> str:
    stem = exe_path.stem
    return stem.replace(".", "_")


def _runtime_module_base(session: "CdbSession", module_name: str) -> int | None:
    text = _require_completed(session.execute([f"lm m {module_name}"], wait_for_stop=False))
    for line in text.splitlines():
        stripped = re.sub(r"^\d+:\d+>\s*", "", line.strip())
        match = re.match(r"^([0-9A-Fa-f`]+)\s+[0-9A-Fa-f`]+\s+\S+", stripped)
        if match:
            return int(match.group(1).replace("`", ""), 16)
    return None


def resolve_source_line_address(
    session: "CdbSession",
    source_file: str,
    line: int,
) -> dict[str, Any] | None:
    exe_path = Path(session.exe_path)
    pdb_path = exe_path.with_suffix(".pdb")
    if not pdb_path.is_file():
        return None
    try:
        modules_text = _run_pdbutil(["dump", "--modules", "--files", str(pdb_path)])
    except RuntimeError:
        # cdb can still bind a deferred source breakpoint through its own
        # symbol reader when llvm-pdbutil rejects an otherwise debuggable PDB.
        return None
    module_ids = _parse_pdb_modules_for_source(modules_text, source_file)
    if not module_ids:
        return None

    try:
        sections = _parse_pe_sections(exe_path)
        module_name = _derive_cdb_module_name(exe_path)
        module_base = _runtime_module_base(session, module_name)
        if module_base is None:
            return None

        candidates: list[dict[str, Any]] = []
        for module_id in module_ids:
            lines_text = _run_pdbutil(["dump", "-l", f"--modi={module_id}", str(pdb_path)])
            for section_index, offset in _parse_pdb_line_records(lines_text, source_file, line):
                section_rva = sections.get(section_index)
                if section_rva is None:
                    continue
                rva = section_rva + offset
                va = module_base + rva
                candidates.append(
                    {
                        "module_id": module_id,
                        "section": section_index,
                        "offset": offset,
                        "rva": rva,
                        "address": f"0x{va:016X}",
                        "module_name": module_name,
                        "pdb_path": str(pdb_path),
                    }
                )
    except RuntimeError:
        return None
    return candidates[0] if candidates else None


def _breakpoint_binding_status(
    bp: dict[str, Any] | None,
    *,
    resolved_from_pdb: dict[str, Any] | None = None,
) -> str:
    if not bp:
        return "failed"
    if bp.get("unresolved"):
        return "pending_deferred"
    if bp.get("resolved") and bp.get("address") and str(bp.get("address")) not in {"", "0", "0x0"}:
        return "bound"
    if resolved_from_pdb is not None:
        return "bound"
    return "failed"


def _breakpoint_binding_diagnostics(
    *,
    source_file: str = "",
    line: int = 0,
    symbol: str = "",
    module: str = "",
    address: str = "",
    resolved_from_pdb: dict[str, Any] | None = None,
    direct_command: str = "",
    bp: dict[str, Any] | None = None,
) -> dict[str, Any]:
    base: dict[str, Any]
    if source_file and line > 0:
        request = {
            "kind": "source",
            "source_file": _cdb_path(source_file),
            "line": line,
        }
        if resolved_from_pdb is not None:
            base = {
                "request": request,
                "binding_mode": "pdb_address_fallback",
                "direct_cdb_command": direct_command,
                "fallback_reason": "cdb file:line breakpoints can remain deferred even with valid PDB line records",
                "pdb_resolution": {
                    "status": "resolved",
                    **resolved_from_pdb,
                },
            }
        else:
            base = {
                "request": request,
                "binding_mode": "source_direct",
                "direct_cdb_command": direct_command,
                "fallback_reason": None,
                "pdb_resolution": {
                    "status": "not_used",
                },
            }
    elif symbol:
        base = {
            "request": {
                "kind": "symbol",
                "module": module or None,
                "symbol": symbol,
            },
            "binding_mode": "symbol_direct",
            "direct_cdb_command": direct_command,
            "fallback_reason": None,
            "pdb_resolution": {
                "status": "not_applicable",
            },
        }
    else:
        base = {
            "request": {
                "kind": "address",
                "address": address,
            },
            "binding_mode": "address_direct",
            "direct_cdb_command": direct_command,
            "fallback_reason": None,
            "pdb_resolution": {
                "status": "not_applicable",
            },
        }
    base["binding_status"] = _breakpoint_binding_status(bp, resolved_from_pdb=resolved_from_pdb)
    return base


def parse_stack_frames(text: str) -> list[dict[str, Any]]:
    frames: list[dict[str, Any]] = []

    def _append_frame(index: int, child_sp: str, _ret_addr: str, site: str) -> None:
        symbol = site
        file_path: str | None = None
        line_no: int | None = None
        module: str | None = None
        address = child_sp.replace("`", "").replace("'", "")
        sym_match = re.match(r"([^!]+)!(.+)", site)
        if sym_match:
            module = sym_match.group(1)
            symbol = sym_match.group(2)
        loc_match = re.search(r"\[([^\]@]+)\s*@\s*(\d+)\]", site)
        if loc_match:
            file_path = loc_match.group(1).strip()
            line_no = int(loc_match.group(2))
        elif re.search(r"\.(?:cpp|cxx|c|hpp?|h)\(\d+\)", site, re.I):
            alt = re.search(r"([^\s(]+\.(?:cpp|cxx|c|hpp?|h))\((\d+)\)", site, re.I)
            if alt:
                file_path = alt.group(1)
                line_no = int(alt.group(2))
        frames.append(
            {
                "index": index,
                "module": module,
                "symbol": symbol,
                "file": file_path,
                "line": line_no,
                "address": address,
                "symbolized": bool(file_path or module),
                "raw": site,
            }
        )

    for line in text.splitlines():
        line = re.sub(r"^\d+:\d+>\s*", "", line.strip())
        if not line or "Child-SP" in line or "Call Site" in line or line.startswith("**"):
            continue
        m = re.match(
            r"^#?\s*(\d+)\s+[`']?([0-9a-fA-F`']+)\s+[`']?([0-9a-fA-F`']+)\s+(.+)$",
            line,
        )
        if m:
            _append_frame(int(m.group(1)), m.group(2), m.group(3), m.group(4).strip())
            continue
        m = re.match(r"^[`']?([0-9a-fA-F`']+)\s+[`']?([0-9a-fA-F`']+)\s+(.+)$", line)
        if m:
            _append_frame(len(frames), m.group(1), m.group(2), m.group(3).strip())
    return frames


def parse_locals(text: str) -> list[dict[str, Any]]:
    scopes: list[dict[str, Any]] = []
    for raw_line in text.splitlines():
        line = re.sub(r"^(?:\d+:\d+>\s*)+", "", raw_line)
        m = re.match(r"^\s*([^\s=]+)\s*=\s*(.+)$", line)
        if not m:
            continue
        name = m.group(1).strip()
        value_text = m.group(2).strip()
        scopes.append(
            {
                "name": name,
                "value_text": value_text,
                "type": None,
                "address": _extract_address(value_text),
            }
        )
    return scopes


def _local_lookup_candidates(expression: str) -> list[tuple[str, str]]:
    expr = expression.strip()
    if not expr:
        return []
    candidates: list[tuple[str, str]] = [(expr, "exact")]
    root_match = re.match(r"^([A-Za-z_]\w*)", expr)
    if root_match:
        root = root_match.group(1)
        if root != expr:
            candidates.append((root, "root"))
    return candidates


def _fallback_expression_eval_from_locals(text: str, expression: str) -> dict[str, Any] | None:
    locals_ = parse_locals(text)
    if not locals_:
        return None
    by_name = {item["name"]: item for item in locals_}
    for candidate, match_mode in _local_lookup_candidates(expression):
        local = by_name.get(candidate)
        if local is None:
            continue
        result = {
            "type": local.get("type"),
            "value_text": local.get("value_text", ""),
            "address": local.get("address"),
            "children_preview": [],
            "error": None,
            "evaluation_mode": "locals_fallback",
            "fallback_match": match_mode,
            "matched_local": candidate,
        }
        if candidate != expression.strip():
            result["lookup_warning"] = (
                f"Resolved local '{candidate}' from expression root; member/subexpression"
                " evaluation still requires debugger expression support."
            )
        return result
    return None


def _filter_locals_by_name(locals_: list[dict[str, Any]], name_filter: str) -> list[dict[str, Any]]:
    needle = name_filter.strip().casefold()
    if not needle:
        return locals_
    return [item for item in locals_ if needle in str(item.get("name", "")).casefold()]


def parse_expression_eval(text: str) -> dict[str, Any]:
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    if not lines:
        return {"type": None, "value_text": "", "address": None, "children_preview": [], "error": "empty output"}
    joined = "\n".join(lines)
    if re.search(
        r"(?:^|\s)(?:Error|Syntax error|No symbols found|Couldn't resolve error at|Unexpected token)",
        joined,
        re.I,
    ):
        return {
            "type": None,
            "value_text": joined,
            "address": None,
            "children_preview": [],
            "error": joined,
        }
    first = lines[0]
    type_match = re.match(r"^([A-Za-z_][\w:<>,\s*&]+)\s+(.+)$", first)
    if type_match and not type_match.group(1).startswith("0x"):
        return {
            "type": type_match.group(1).strip(),
            "value_text": type_match.group(2).strip(),
            "address": _extract_address(joined),
            "children_preview": lines[1:8],
            "error": None,
        }
    return {
        "type": None,
        "value_text": joined,
        "address": _extract_address(joined),
        "children_preview": lines[1:8],
        "error": None,
    }


def parse_memory_read(text: str) -> dict[str, Any]:
    bytes_hex: list[str] = []
    ascii_parts: list[str] = []
    u64_values: list[int] = []
    for line in text.splitlines():
        m = re.match(
            r"^[`']?([0-9a-fA-F`']+)\s+((?:[0-9a-fA-F]{2}\s+)+)(.*)$",
            line.strip(),
        )
        if not m:
            continue
        chunk = re.findall(r"[0-9a-fA-F]{2}", m.group(2))
        bytes_hex.extend(chunk)
        tail = m.group(3) or ""
        ascii_parts.append(re.sub(r"[^\x20-\x7e]", ".", tail))
        if len(chunk) >= 8:
            try:
                u64_values.append(int("".join(reversed(chunk[:8])), 16))
            except ValueError:
                pass
    return {
        "bytes_hex": " ".join(bytes_hex),
        "ascii": "".join(ascii_parts),
        "u64": u64_values[:16],
    }


def parse_registers(text: str) -> dict[str, str]:
    registers: dict[str, str] = {}
    for line in text.splitlines():
        for name, value in re.findall(r"\b(r[a-z0-9]+|e[a-z0-9]+|rip|rsp|rbp|rax|rbx|rcx|rdx)\s*=\s*([0-9a-fA-F`']+)", line, re.I):
            registers[name.lower()] = value.replace("`", "").replace("'", "")
    return registers


def parse_threads(text: str) -> list[dict[str, Any]]:
    threads: list[dict[str, Any]] = []
    for line in text.splitlines():
        m = re.match(r"^\s*(\d+)\s+id:\s*([0-9a-fx.]+)\s+([^\s]+)\s+(.+)$", line, re.I)
        if m:
            threads.append(
                {
                    "index": int(m.group(1)),
                    "id": m.group(2),
                    "state": m.group(3),
                    "info": m.group(4).strip(),
                }
            )
    return threads


def parse_modules_list(text: str) -> list[dict[str, Any]]:
    modules: list[dict[str, Any]] = []
    for line in text.splitlines():
        stripped = re.sub(r"^(?:\d+:\d+>\s*)+", "", line.strip())
        match = re.match(
            r"^([0-9a-fA-F`']+)\s+([0-9a-fA-F`']+)\s+(\S+)\s+(.+)$",
            stripped,
        )
        if not match:
            continue
        modules.append(
            {
                "base": match.group(1).replace("`", "").replace("'", ""),
                "end": match.group(2).replace("`", "").replace("'", ""),
                "module": match.group(3),
                "path": match.group(4).strip(),
            }
        )
    return modules


def parse_heap_summary(text: str) -> dict[str, Any]:
    summary: dict[str, Any] = {"heaps": []}
    current: dict[str, Any] | None = None
    for line in text.splitlines():
        stripped = re.sub(r"^(?:\d+:\d+>\s*)+", "", line.strip())
        heap_match = re.match(r"^Heap\s+(\d+)(?:\s+(.+))?$", stripped, re.I)
        if heap_match:
            current = {"heap_index": int(heap_match.group(1)), "flags": (heap_match.group(2) or "").strip()}
            summary["heaps"].append(current)
            continue
        if current is None:
            continue
        for key, pattern in (
            ("segments", r"(\d+)\s+segment"),
            ("reserved_bytes", r"(\d[\d,]*)\s+bytes reserved"),
            ("committed_bytes", r"(\d[\d,]*)\s+bytes committed"),
            ("uncommitted_bytes", r"(\d[\d,]*)\s+bytes uncommitted"),
        ):
            match = re.search(pattern, stripped, re.I)
            if match:
                value = match.group(1).replace(",", "")
                current[key] = int(value) if value.isdigit() else value
    return summary


def parse_breakpoint_list(text: str) -> list[dict[str, Any]]:
    breakpoints: list[dict[str, Any]] = []
    for line in text.splitlines():
        stripped = re.sub(r"^(?:\d+:\d+>\s*)+", "", line.strip())
        if not stripped or stripped.startswith("bl") or stripped.startswith("***"):
            continue
        m = re.match(r"^(\d+)\s+(\S+)\s+", stripped)
        if not m:
            continue
        bp_id = int(m.group(1))
        state = m.group(2).lower()
        enabled = not state.startswith("d")
        address_match = re.search(r"\b(0x[0-9a-fA-F`']+|[0-9a-fA-F`']{8,})\b", stripped)
        address = address_match.group(1).replace("`", "").replace("'", "") if address_match else None
        location = stripped
        loc_match = re.search(r"\(`([^`]+)`:(\d+)\)", stripped)
        if loc_match:
            location = f"{loc_match.group(1)}:{loc_match.group(2)}"
        elif " bu " in stripped:
            location = stripped.split(" bu ", 1)[1]
        elif " bp " in stripped:
            location = stripped.split(" bp ", 1)[1]
        unresolved = "u" in state
        breakpoints.append(
            {
                "breakpoint_id": bp_id,
                "enabled": enabled,
                "resolved": not unresolved and bool(address and address != "0"),
                "resolved_location": location.strip(),
                "address": address,
                "unresolved": unresolved,
            }
        )
    return breakpoints


def parse_stop_event(text: str) -> dict[str, Any]:
    reason = "step_complete"
    if re.search(r"breakpoint\s+\d+\s+hit", text, re.I):
        reason = "breakpoint"
    elif re.search(r"exception|access violation|0xc0000005|wow64 breakpoint", text, re.I):
        reason = "exception"
    elif re.search(r"process exited|wow64 exited|exit code", text, re.I):
        reason = "process_exit"
    elif re.search(r"debug break|dbgBreakPoint|control-c|interrupt", text, re.I):
        reason = "manual_interrupt"
    elif re.search(r"initial breakpoint|ntdll!LdrpDoDebuggerBreak|module load", text, re.I):
        reason = "initial_stop"

    bp_match = re.search(r"breakpoint\s+(\d+)\s+hit", text, re.I)
    breakpoint = None
    if bp_match:
        breakpoint = {
            "breakpoint_id": int(bp_match.group(1)),
            "location": _extract_breakpoint_location(text, int(bp_match.group(1))),
        }

    exception = None
    exc_match = re.search(
        r"(?:Exception|EXCEPTION)[^\n]*?(0x[0-9a-fA-F]+)",
        text,
        re.I,
    )
    if exc_match or reason == "exception":
        exception = {
            "code": exc_match.group(1) if exc_match else None,
            "first_chance": bool(re.search(r"first chance", text, re.I)),
            "address": _extract_address(text),
            "description": _first_matching_line(
                text,
                r"(access violation|exception|fatal|heap corruption)",
            ),
        }

    frames = parse_stack_frames(text)
    frame = frames[0] if frames else None
    thread_match = re.search(r"thread\s+#\s*(\d+)", text, re.I)
    return {
        "reason": reason,
        "thread_id": int(thread_match.group(1)) if thread_match else None,
        "frame": frame,
        "exception": exception,
        "breakpoint": breakpoint,
    }


def _extract_address(text: str) -> str | None:
    match = re.search(r"0x[0-9a-fA-F`']+", text)
    if not match:
        return None
    return match.group(0).replace("`", "").replace("'", "")


def _extract_breakpoint_location(text: str, breakpoint_id: int) -> str | None:
    lines = text.splitlines()
    saw_hit = False
    for raw_line in lines:
        line = re.sub(r"^(?:\d+:\d+>\s*)+", "", raw_line.strip())
        if not line:
            continue
        if re.search(rf"breakpoint\s+{breakpoint_id}\s+hit", line, re.I):
            saw_hit = True
            continue
        if not saw_hit:
            continue
        if line.startswith("ModLoad:") or MCP_MARKER_PREFIX in line:
            continue
        if re.match(r"^[0-9A-Fa-f`']+\s", line):
            continue
        return line
    for raw_line in lines:
        line = re.sub(r"^(?:\d+:\d+>\s*)+", "", raw_line.strip())
        if re.search(rf"breakpoint\s+{breakpoint_id}\s+hit", line, re.I):
            return line
    return None


def _first_matching_line(text: str, pattern: str) -> str | None:
    for line in text.splitlines():
        if re.search(pattern, line, re.I):
            return line.strip()
    return None


@dataclass
class CdbSession:
    session_id: str
    proc: subprocess.Popen[str]
    output_queue: queue.Queue[str | None]
    reader_thread: threading.Thread
    state: str = "launching"
    last_stop_event: dict[str, Any] | None = None
    current_frame: int = 0
    current_thread: int | None = None
    breakpoints: dict[int, dict[str, Any]] = field(default_factory=dict)
    diagnostics: list[str] = field(default_factory=list)
    operation_log: list[dict[str, Any]] = field(default_factory=list)
    current_operation: dict[str, Any] | None = None
    process_id: int | None = None
    inferior_pid: int | None = None
    cdb_pid: int | None = None
    attached: bool = False
    exit_code: int | None = None
    cdb_path: str = ""
    exe_path: str = ""
    ready: bool = False
    reload_symbols: bool = True
    created_at: float = field(default_factory=time.time)
    last_activity_at: float = field(default_factory=time.time)
    last_output_at: float | None = None

    def _record_operation(self, operation: CdbOperationResult) -> None:
        self.last_activity_at = time.time()
        self.last_output_at = self.last_activity_at if operation.text else self.last_output_at
        entry = operation.as_payload()
        entry["state_after"] = self.state
        self.operation_log.append(entry)
        if len(self.operation_log) > MAX_OPERATION_LOG:
            self.operation_log = self.operation_log[-MAX_OPERATION_LOG:]
        if self.current_operation is not None:
            self.current_operation["status"] = operation.status
            self.current_operation["marker_seen"] = operation.marker_seen
            self.current_operation["elapsed_ms"] = operation.elapsed_ms
            self.current_operation["error_message"] = operation.error_message
            self.current_operation["completed_at"] = time.time()
            self.current_operation["timed_out"] = operation.status == "timeout"
            self.current_operation["raw_preview"] = operation.as_payload()["raw_preview"]
            if operation.status != "timeout":
                self.current_operation = None
        self._persist()

    def _begin_operation(self, commands: list[str], *, timeout_ms: int, wait_for_stop: bool) -> dict[str, Any]:
        now = time.time()
        self.current_operation = {
            "commands": list(commands),
            "started_at": now,
            "deadline_at": now + timeout_ms / 1000.0,
            "timeout_ms": timeout_ms,
            "wait_for_stop": wait_for_stop,
            "status": "running",
        }
        self._persist()
        return self.current_operation

    def _persist(self) -> None:
        _persist_session_metadata(self.snapshot())

    def _try_capture_inferior_pid(self, text: str) -> None:
        if self.inferior_pid is not None:
            return
        match = re.search(r"process\s+is\s+([0-9a-fx.]+)", text, re.I)
        if not match:
            match = re.search(r"Debuggee\s+PID:\s*(\d+)", text, re.I)
        if match:
            try:
                raw = match.group(1)
                self.inferior_pid = int(raw, 0) if raw.lower().startswith("0x") else int(raw)
            except ValueError:
                pass

    @classmethod
    def start(
        cls,
        *,
        exe: str,
        args: list[str],
        cwd: str | None,
        env: dict[str, str] | None,
        symbols: str | list[str] | None,
        break_on_start: bool,
        process_id: int | None = None,
        reload_symbols: bool = True,
        startup_timeout_ms: int = DEFAULT_STARTUP_TIMEOUT_MS,
        wait_ready: bool = True,
    ) -> "CdbSession":
        cdb_info = find_cdb()
        if not cdb_info["found"]:
            raise RuntimeError("cdb.exe not found")

        session_id = str(uuid.uuid4())
        workdir = str(Path(cwd).resolve()) if cwd else str(repo_root())
        merged_env = os.environ.copy()
        if env:
            merged_env.update(env)

        init_cmds = [".symfix"]
        if reload_symbols:
            init_cmds.append(".reload")
        init_cmds.append(".lines -e")
        marker = f"{MCP_MARKER_PREFIX}READY_{session_id}__"
        init_cmds.append(f".echo {marker}")

        attached = process_id is not None
        exe_path_str = f"<attached:{process_id}>" if attached else str(_resolve_exe(exe, cwd))
        if attached:
            if process_id is None or process_id <= 0:
                raise RuntimeError("process_id must be a positive integer")
            cmd = [
                cdb_info["path"],
                "-o",
                "-p",
                str(process_id),
                "-c",
                "; ".join(init_cmds),
            ]
        else:
            cmd = [
                cdb_info["path"],
                "-o",
                "-c",
                "; ".join(init_cmds),
                exe_path_str,
                *args,
            ]
        proc = subprocess.Popen(
            cmd,
            cwd=workdir,
            env=merged_env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            bufsize=1,
            creationflags=(getattr(subprocess, "CREATE_NEW_PROCESS_GROUP", 0) if os.name == "nt" else 0),
        )
        output_queue: queue.Queue[str | None] = queue.Queue()

        def _reader() -> None:
            assert proc.stdout is not None
            for line in proc.stdout:
                output_queue.put(line)
            output_queue.put(None)

        thread = threading.Thread(target=_reader, daemon=True)
        thread.start()
        session = cls(
            session_id=session_id,
            proc=proc,
            output_queue=output_queue,
            reader_thread=thread,
            cdb_path=cdb_info["path"],
            exe_path=exe_path_str,
            process_id=process_id,
            inferior_pid=process_id if attached else None,
            cdb_pid=proc.pid,
            attached=attached,
            reload_symbols=reload_symbols,
        )
        session._persist()
        ready_op = session._drain_until(marker, timeout_ms=startup_timeout_ms)
        session._record_operation(ready_op)
        if ready_op.status == "timeout" and not wait_ready:
            session.diagnostics.append("startup still in progress; poll debug_session_wait_ready")
            session.state = "launching"
            session._persist()
            return session
        if ready_op.status != "completed" or not ready_op.marker_seen:
            session.state = "error"
            session._persist()
            preview = ready_op.as_payload()["raw_preview"]
            raise RuntimeError(
                "cdb did not become ready"
                + (f": {preview}" if preview else " (no output)")
            )
        session.ready = True
        session._try_capture_inferior_pid(ready_op.text)
        if symbols:
            for sym_path in symbols:
                path = _cdb_path(sym_path).strip()
                if path:
                    session.execute([f".sympath+ {path}"], wait_for_stop=False)
        session.state = "stopped"
        session.last_stop_event = parse_stop_event(ready_op.text)
        session.last_stop_event["reason"] = "initial_stop"
        session._persist()
        if not break_on_start:
            session._continue_internal(timeout_ms=DEFAULT_TIMEOUT_MS)
        return session

    def wait_ready(self, *, timeout_ms: int = DEFAULT_STARTUP_TIMEOUT_MS) -> CdbOperationResult:
        if self.ready:
            return CdbOperationResult(
                status="completed",
                text="",
                marker_seen=True,
                command_id=str(uuid.uuid4()),
                commands=["wait_ready"],
            )
        marker = f"{MCP_MARKER_PREFIX}READY_{self.session_id}__"
        if marker in self._peek_buffered_output():
            self.ready = True
            self.state = "stopped"
            self._persist()
            return CdbOperationResult(
                status="completed",
                text="",
                marker_seen=True,
                command_id=str(uuid.uuid4()),
                commands=["wait_ready"],
            )
        op = self._drain_until(marker, timeout_ms=timeout_ms)
        self._record_operation(op)
        if op.status == "completed" and op.marker_seen:
            self.ready = True
            self._try_capture_inferior_pid(op.text)
            self.state = "stopped"
            self.last_stop_event = parse_stop_event(op.text)
            self.last_stop_event["reason"] = "initial_stop"
        elif op.status == "timeout":
            self.state = "launching"
        self._persist()
        return op

    def _peek_buffered_output(self) -> str:
        chunks: list[str] = []
        while True:
            try:
                line = self.output_queue.get_nowait()
            except queue.Empty:
                break
            if line is None:
                self.state = "exited"
                self.exit_code = self.proc.returncode
                break
            chunks.append(line)
        return "".join(chunks)

    def _drain_until(self, marker: str, *, timeout_ms: int) -> CdbOperationResult:
        command_id = str(uuid.uuid4())
        started = time.time()
        chunks: list[str] = []
        marker_seen = False
        deadline = started + timeout_ms / 1000.0
        status = "completed"
        error_message: str | None = None
        while time.time() < deadline:
            if self.proc.poll() is not None:
                self.state = "exited"
                self.exit_code = self.proc.returncode
                status = "process_exited"
                break
            try:
                line = self.output_queue.get(timeout=0.1)
            except queue.Empty:
                continue
            if line is None:
                self.state = "exited"
                self.exit_code = self.proc.returncode
                status = "process_exited"
                break
            chunks.append(line)
            self._try_capture_inferior_pid(line)
            if marker in line:
                marker_seen = True
                break
        else:
            status = "timeout"
            error_message = f"marker not seen within {timeout_ms}ms"
        text, truncated = _truncate("".join(chunks))
        if truncated:
            self.diagnostics.append("output truncated while waiting for marker")
        elapsed_ms = (time.time() - started) * 1000.0
        if status == "timeout":
            self.diagnostics.append(error_message or "operation timed out")
        return CdbOperationResult(
            status=status,
            text=text,
            marker_seen=marker_seen,
            truncated=truncated,
            elapsed_ms=elapsed_ms,
            command_id=command_id,
            commands=[f"wait:{marker}"],
            error_message=error_message,
        )

    def execute(
        self,
        commands: list[str],
        *,
        timeout_ms: int = DEFAULT_TIMEOUT_MS,
        wait_for_stop: bool = False,
    ) -> CdbOperationResult:
        if self.current_operation and self.current_operation.get("timed_out") and commands != [".echo interrupt_ack"]:
            return CdbOperationResult(
                status="backend_error",
                text="",
                command_id=str(uuid.uuid4()),
                commands=list(commands),
                error_message="session requires interrupt or stop after timed out operation",
            )
        if self.proc.poll() is not None:
            self.state = "exited"
            self.exit_code = self.proc.returncode
            self._persist()
            return CdbOperationResult(
                status="process_exited",
                text="",
                command_id=str(uuid.uuid4()),
                commands=list(commands),
                error_message="debug session process already exited",
            )
        if self.proc.stdin is None:
            return CdbOperationResult(
                status="backend_error",
                text="",
                command_id=str(uuid.uuid4()),
                commands=list(commands),
                error_message="cdb stdin is unavailable",
            )

        command_id = str(uuid.uuid4())
        marker = f"{MCP_MARKER_PREFIX}DONE_{uuid.uuid4().hex}__"
        script = list(commands) + [f".echo {marker}"]
        payload = "\n".join(script) + "\n"
        started = time.time()
        self._begin_operation(commands, timeout_ms=timeout_ms, wait_for_stop=wait_for_stop)
        try:
            self.proc.stdin.write(payload)
            self.proc.stdin.flush()
        except Exception as exc:
            self.current_operation = None
            self._persist()
            return CdbOperationResult(
                status="backend_error",
                text="",
                command_id=command_id,
                commands=list(commands),
                error_message=str(exc),
            )

        chunks: list[str] = []
        marker_seen = False
        deadline = started + timeout_ms / 1000.0
        status = "completed"
        error_message: str | None = None
        while time.time() < deadline:
            if self.proc.poll() is not None:
                self.state = "exited"
                self.exit_code = self.proc.returncode
                status = "process_exited"
                break
            try:
                line = self.output_queue.get(timeout=0.1)
            except queue.Empty:
                continue
            if line is None:
                self.state = "exited"
                self.exit_code = self.proc.returncode
                status = "process_exited"
                break
            chunks.append(line)
            self._try_capture_inferior_pid(line)
            if marker in line:
                marker_seen = True
                break
        else:
            status = "timeout"
            error_message = f"marker not seen within {timeout_ms}ms"

        text, truncated = _truncate("".join(chunks))
        if truncated:
            self.diagnostics.append("command output truncated")
        elapsed_ms = (time.time() - started) * 1000.0
        operation = CdbOperationResult(
            status=status,
            text=text,
            marker_seen=marker_seen,
            truncated=truncated,
            elapsed_ms=elapsed_ms,
            command_id=command_id,
            commands=list(commands),
            error_message=error_message,
        )
        self._record_operation(operation)

        if wait_for_stop and status == "completed":
            self.state = "stopped"
            self.last_stop_event = parse_stop_event(text)
        elif self.proc.poll() is not None:
            self.state = "exited"
            if status == "completed":
                self.last_stop_event = parse_stop_event(text)
        elif status == "completed":
            if self.ready:
                self.state = "stopped"
        elif status == "timeout" and wait_for_stop:
            self.state = "running"
        self._persist()
        return operation

    def interrupt(self, *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
        if self.proc.poll() is not None:
            self.state = "exited"
            self.exit_code = self.proc.returncode
            self._persist()
            raise RuntimeError("debug session process already exited")
        if os.name == "nt":
            try:
                self.proc.send_signal(signal.CTRL_BREAK_EVENT)
                self.diagnostics.append("sent CTRL_BREAK_EVENT to cdb process group")
            except Exception as exc:
                self.diagnostics.append(f"CTRL_BREAK_EVENT failed: {exc}")
        text = _require_completed(self.execute([".echo interrupt_ack"], timeout_ms=timeout_ms, wait_for_stop=True))
        stop_event = parse_stop_event(text)
        stop_event["reason"] = "manual_interrupt"
        self.last_stop_event = stop_event
        self.current_operation = None
        self.state = "stopped"
        self._persist()
        return {"stop_event": stop_event, "raw": _truncate(text)[0]}

    def stop(self, *, kill_process: bool = True) -> dict[str, Any]:
        final_state = self.state
        cleanup_actions: list[str] = []
        quit_command = "q"
        if self.attached and not kill_process:
            quit_command = "qd"
        if self.proc.poll() is None:
            try:
                if self.proc.stdin is not None:
                    self.proc.stdin.write(f"{quit_command}\n")
                    self.proc.stdin.flush()
                    cleanup_actions.append(f"cdb_quit:{quit_command}")
            except Exception:
                pass
            try:
                self.proc.wait(timeout=2)
            except subprocess.TimeoutExpired:
                self.proc.kill()
                cleanup_actions.append("cdb_kill")
        if kill_process:
            if self.inferior_pid:
                if _kill_process_tree(self.inferior_pid):
                    cleanup_actions.append(f"inferior_tree_kill:{self.inferior_pid}")
            elif self.cdb_pid:
                if _kill_process_tree(self.cdb_pid):
                    cleanup_actions.append(f"cdb_tree_kill:{self.cdb_pid}")
        if self.proc.poll() is not None:
            final_state = "exited"
            self.exit_code = self.proc.returncode
        for stream in (self.proc.stdin, self.proc.stdout):
            if stream is not None:
                try:
                    stream.close()
                except Exception:
                    pass
        self.current_operation = None
        self.state = final_state
        _remove_persisted_session(self.session_id)
        return {
            "final_state": final_state,
            "exit_code": self.exit_code,
            "cleanup_actions": cleanup_actions,
            "cdb_pid": self.cdb_pid,
            "inferior_pid": self.inferior_pid,
        }

    def _continue_internal(self, *, timeout_ms: int) -> dict[str, Any]:
        operation = self.execute(["g"], timeout_ms=timeout_ms, wait_for_stop=True)
        text = _require_completed(operation)
        return parse_stop_event(text)

    def snapshot(self) -> dict[str, Any]:
        process_id = self.process_id if isinstance(self.process_id, int) else None
        inferior_pid = self.inferior_pid if isinstance(self.inferior_pid, int) else None
        cdb_pid = self.cdb_pid if isinstance(self.cdb_pid, int) else None
        exit_code = self.exit_code if isinstance(self.exit_code, int) else None
        return {
            "session_id": self.session_id,
            "state": self.state,
            "ready": self.ready,
            "active_in_process": True,
            "orphaned": False,
            "attached": self.attached,
            "process_id": process_id,
            "inferior_pid": inferior_pid,
            "cdb_pid": cdb_pid,
            "exe": self.exe_path,
            "exit_code": exit_code,
            "created_at": self.created_at,
            "last_activity_at": self.last_activity_at,
            "last_output_at": self.last_output_at,
            "current_operation": dict(self.current_operation) if self.current_operation else None,
            "operation_log_tail": list(self.operation_log[-5:]),
        }


def _get_session(session_id: str) -> CdbSession:
    with _SESSION_LOCK:
        session = _SESSIONS.get(session_id)
    if session is None:
        raise KeyError(session_id)
    return session


def _session_or_error(session_id: str) -> tuple[CdbSession | None, dict[str, Any] | None]:
    try:
        return _get_session(session_id), None
    except KeyError:
        return None, _envelope(
            ok=False,
            session_id=session_id,
            state=None,
            summary="Debug session not found",
            error=_error(
                "session_not_found",
                f"Unknown session_id: {session_id}",
                recoverable=False,
                suggested_next_actions=["Call debug_session_start to create a new session."],
            ),
        )


def _wrap_session_call(
    session_id: str,
    summary: str,
    fn: Callable[[CdbSession], dict[str, Any]],
) -> dict[str, Any]:
    session, error = _session_or_error(session_id)
    if error is not None:
        return error
    assert session is not None
    try:
        data = fn(session)
        return _envelope(
            ok=True,
            session_id=session_id,
            state=session.state,
            summary=summary,
            data=data,
            diagnostics=list(session.diagnostics),
        )
    except CdbOperationTimeout as exc:
        session.state = "error" if session.state not in {"launching", "running"} else session.state
        return _envelope(
            ok=False,
            session_id=session_id,
            state=session.state,
            summary="Debugger command timed out",
            error=_error(
                "timeout",
                str(exc),
                recoverable=True,
                suggested_next_actions=[
                    "Retry with a larger timeout_ms.",
                    "Call debug_interrupt.",
                    "Call debug_session_status.",
                    "Call debug_session_stop if recovery fails.",
                ],
            ),
            data={"operation": exc.operation.as_payload()},
            diagnostics=list(session.diagnostics),
        )
    except subprocess.TimeoutExpired:
        session.state = "error"
        return _envelope(
            ok=False,
            session_id=session_id,
            state=session.state,
            summary="Debugger command timed out",
            error=_error(
                "timeout",
                "cdb command exceeded timeout",
                recoverable=True,
                suggested_next_actions=["Retry with a larger timeout_ms.", "Call debug_session_status."],
            ),
            diagnostics=list(session.diagnostics),
        )
    except Exception as exc:
        session.state = "error"
        return _envelope(
            ok=False,
            session_id=session_id,
            state=session.state,
            summary="Debugger backend error",
            error=_error(
                "backend_error",
                str(exc),
                recoverable=False,
                suggested_next_actions=["Call debug_session_stop and start a new session."],
            ),
            diagnostics=list(session.diagnostics),
        )


def debug_session_start(
    *,
    exe: str = "",
    exe_target: str = "",
    config: str = "",
    process_id: int = 0,
    process_name: str = "",
    args: list[str] | None = None,
    cwd: str | None = None,
    env: dict[str, str] | None = None,
    symbols: str | None = None,
    break_on_start: bool = True,
    wait_ready: bool = True,
    startup_timeout_ms: int = DEFAULT_STARTUP_TIMEOUT_MS,
    reload_symbols: bool = True,
) -> dict[str, Any]:
    root = repo_root()
    requested_attach = process_id > 0 or bool(process_name.strip())
    if requested_attach and any([exe, exe_target, config, args]):
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Attach mode does not accept launch arguments",
            error=_error(
                "invalid_input",
                "process_id/process_name cannot be combined with exe/exe_target/config/args",
                recoverable=True,
                suggested_next_actions=[
                    "Use only process_id or process_name (plus optional cwd/symbols/break_on_start) for attach.",
                    "Or omit process_id and start a new inferior normally.",
                ],
            ),
        )
    if process_id < 0:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Invalid process_id",
            error=_error(
                "invalid_input",
                "process_id must be >= 0",
                recoverable=True,
            ),
        )
    resolved_process_id, attach_error = _resolve_process_attach_target(
        process_id=process_id,
        process_name=process_name,
    )
    if attach_error is not None:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Failed to resolve attach target",
            error=_error(
                attach_error["kind"],
                attach_error["message"],
                recoverable=bool(attach_error.get("recoverable")),
                suggested_next_actions=list(attach_error.get("suggested_next_actions", [])),
            ),
            data={
                "process_name_query": process_name or None,
                "candidates": list(attach_error.get("candidates", [])),
            },
        )
    attach_mode = resolved_process_id is not None

    resolution = None
    arg_list: list[str] = list(args or [])
    resolved_exe = ""
    workdir = cwd or str(root)
    if not attach_mode:
        resolution = resolve_exe(
            root,
            exe_target=exe_target or None,
            exe_path=exe or None,
            config=config or None,
        )
        resolved_exe = str(resolution.exe)
        workdir = cwd or str(resolution.cwd)
    sym_paths = symbols.split(";") if symbols else (default_symbol_paths(root, resolution.exe) if resolution else [])
    if isinstance(sym_paths, str):
        sym_paths = [part.strip() for part in sym_paths.split(";") if part.strip()]
    cdb_info = find_cdb()
    if not cdb_info["found"]:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="cdb.exe not found",
            error=_error(
                "launch_failed",
                "Install windows-debuggers via vcpkg or set CRASH_ANALYSIS_DEBUGGER_PATH.",
                recoverable=False,
                suggested_next_actions=["Install windows-debuggers via vcpkg.", "Set CRASH_ANALYSIS_DEBUGGER_PATH to cdb.exe."],
            ),
            data={
                "cdb": cdb_info,
                "resolved_exe": resolution.as_payload() if resolution else None,
                "attach_process_id": resolved_process_id if attach_mode else None,
                "process_name_query": process_name or None,
            },
        )
    try:
        session = CdbSession.start(
            exe=resolved_exe,
            args=arg_list,
            cwd=workdir,
            env=env,
            symbols=sym_paths,
            break_on_start=break_on_start,
            process_id=resolved_process_id if attach_mode else None,
            reload_symbols=reload_symbols,
            startup_timeout_ms=startup_timeout_ms,
            wait_ready=wait_ready,
        )
    except FileNotFoundError as exc:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Executable not found",
            error=_error(
                "launch_failed",
                str(exc),
                recoverable=True,
                suggested_next_actions=[
                    "Build the target first (e.g. cmake --build --preset debug --target EpicMapEditor).",
                    "Pass exe or exe_target explicitly.",
                ],
            ),
            data={
                "cdb": cdb_info,
                "resolved_exe": resolution.as_payload() if resolution else None,
                "process_name_query": process_name or None,
            },
        )
    except Exception as exc:
        return _envelope(
            ok=False,
            session_id=None,
            state="error",
            summary="Failed to start debug session",
            error=_error(
                "launch_failed",
                str(exc),
                recoverable=False,
                suggested_next_actions=(
                    ["Verify process_id/process_name and retry attach.", "Retry with break_on_start=true."]
                    if attach_mode
                    else ["Verify cdb path and executable.", "Retry with break_on_start=true."]
                ),
            ),
            data={
                "cdb": cdb_info,
                "resolved_exe": resolution.as_payload() if resolution else None,
                "attach_process_id": resolved_process_id if attach_mode else None,
                "process_name_query": process_name or None,
            },
        )

    with _SESSION_LOCK:
        _SESSIONS[session.session_id] = session
    launching = session.state == "launching" and not session.ready
    return _envelope(
        ok=True,
        session_id=session.session_id,
        state=session.state,
        summary=(
            "Debug session launching; poll debug_session_wait_ready"
            if launching
            else (
                f"Attached debug session to process {resolved_process_id}"
                if attach_mode
                else f"Started debug session for {session.exe_path}"
            )
        ),
        data={
            "process_id": session.process_id,
            "inferior_pid": session.inferior_pid,
            "cdb_pid": session.cdb_pid,
            "attached": session.attached,
            "ready": session.ready,
            "process_name_query": process_name or None,
            "backend": "cdb",
            "cdb": cdb_info,
            "exe": session.exe_path,
            "resolved_exe": resolution.as_payload() if resolution else None,
            "args": arg_list,
            "cwd": workdir,
            "symbols": sym_paths,
            "break_on_start": break_on_start,
            "wait_ready": wait_ready,
            "startup_timeout_ms": startup_timeout_ms,
            "reload_symbols": reload_symbols,
            "last_stop_event": session.last_stop_event,
        },
        diagnostics=list(session.diagnostics),
    )


def debug_launch_then_attach(
    *,
    exe: str = "",
    exe_target: str = "",
    config: str = "",
    args: list[str] | None = None,
    cwd: str | None = None,
    env: dict[str, str] | None = None,
    attach_delay_ms: int = 3000,
    breakpoints: list[dict[str, Any]] | None = None,
    break_on_attach: bool = True,
    wait_ready: bool = True,
    startup_timeout_ms: int = DEFAULT_STARTUP_TIMEOUT_MS,
    reload_symbols: bool = False,
) -> dict[str, Any]:
    root = repo_root()
    arg_list = list(args or [])
    breakpoint_specs = list(breakpoints or [])
    if attach_delay_ms < 0:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Invalid attach delay",
            error=_error("invalid_input", "attach_delay_ms must be >= 0", recoverable=True),
        )
    if not isinstance(breakpoint_specs, list):
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Invalid breakpoint list",
            error=_error("invalid_input", "breakpoints must be a list", recoverable=True),
        )
    extra_env = dict(env or {})
    invalid_env_keys = [
        key
        for key, value in extra_env.items()
        if not isinstance(key, str) or not isinstance(value, str)
    ]
    if invalid_env_keys:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Invalid launch environment",
            error=_error(
                "invalid_input",
                "env must contain only string keys and string values",
                recoverable=True,
            ),
        )

    try:
        resolution = resolve_exe(
            root,
            exe_target=exe_target or None,
            exe_path=exe or None,
            config=config or None,
        )
    except Exception as exc:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Executable not found",
            error=_error(
                "launch_failed",
                str(exc),
                recoverable=True,
                suggested_next_actions=[
                    "Build the target first (e.g. cmake --build --preset debug --target EpicMapEditor).",
                    "Pass exe or exe_target explicitly.",
                ],
            ),
        )

    workdir = cwd or str(resolution.cwd)
    merged_env = os.environ.copy()
    merged_env.update(extra_env)
    log_dir = root / "test_logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    launch_id = str(uuid.uuid4())
    stdout_path = log_dir / f"debug_launch_then_attach_{launch_id}.stdout.log"
    stderr_path = log_dir / f"debug_launch_then_attach_{launch_id}.stderr.log"
    proc: subprocess.Popen[Any] | None = None
    stdout_handle = None
    stderr_handle = None
    try:
        stdout_handle = stdout_path.open("w", encoding="utf-8")
        stderr_handle = stderr_path.open("w", encoding="utf-8")
        proc = subprocess.Popen(
            [str(resolution.exe), *arg_list],
            cwd=workdir,
            env=merged_env,
            stdout=stdout_handle,
            stderr=stderr_handle,
            text=True,
        )
    except Exception as exc:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Failed to launch process before attach",
            error=_error(
                "launch_failed",
                str(exc),
                recoverable=True,
                suggested_next_actions=["Verify exe/cwd and retry."],
            ),
            data={
                "resolved_exe": resolution.as_payload(),
                "args": arg_list,
                "cwd": workdir,
                "stdout_path": str(stdout_path),
                "stderr_path": str(stderr_path),
            },
        )
    finally:
        for handle in (stdout_handle, stderr_handle):
            if handle is not None:
                try:
                    handle.close()
                except Exception:
                    pass

    launched_pid = int(proc.pid)
    time.sleep(attach_delay_ms / 1000.0)
    exit_code = proc.poll()
    base_data: dict[str, Any] = {
        "launched_pid": launched_pid,
        "attached": False,
        "attach_delay_ms": attach_delay_ms,
        "resolved_exe": resolution.as_payload(),
        "exe": str(resolution.exe),
        "args": arg_list,
        "cwd": workdir,
        "stdout_path": str(stdout_path),
        "stderr_path": str(stderr_path),
        "breakpoint_results": None,
        "process_exit_code": exit_code,
    }
    if exit_code is not None:
        return _envelope(
            ok=False,
            session_id=None,
            state="exited",
            summary="Process exited before delayed attach",
            error=_error(
                "process_exited",
                f"process {launched_pid} exited before attach with code {exit_code}",
                recoverable=True,
                suggested_next_actions=["Increase attach_delay_ms only if the process should stay alive longer."],
            ),
            data=base_data,
        )

    symbols = ";".join(default_symbol_paths(root, resolution.exe))
    attach_result = debug_session_start(
        process_id=launched_pid,
        cwd=workdir,
        env=extra_env,
        symbols=symbols,
        break_on_start=break_on_attach,
        wait_ready=wait_ready,
        startup_timeout_ms=startup_timeout_ms,
        reload_symbols=reload_symbols,
    )
    session_id = attach_result.get("session_id")
    attach_data = dict(attach_result.get("data") or {})
    data = {**base_data, **attach_data, "launched_pid": launched_pid, "attach_delay_ms": attach_delay_ms}
    data["attached"] = bool(attach_result.get("ok") and attach_data.get("attached", True))
    data["stdout_path"] = str(stdout_path)
    data["stderr_path"] = str(stderr_path)

    if not attach_result.get("ok"):
        killed = _kill_process_tree(launched_pid)
        data["cleanup_action"] = "killed_launched_process" if killed else "kill_launched_process_failed"
        return _envelope(
            ok=False,
            session_id=session_id,
            state=attach_result.get("state"),
            summary="Failed to attach CDB to launched process",
            error=attach_result.get("error")
            or _error("launch_failed", "debug_session_start attach failed", recoverable=True),
            data=data,
            diagnostics=list(attach_result.get("diagnostics") or []),
        )

    if breakpoint_specs:
        breakpoint_result = debug_breakpoint_set_bulk(str(session_id), breakpoint_specs)
        data["breakpoint_results"] = breakpoint_result
        if not breakpoint_result.get("ok"):
            return _envelope(
                ok=False,
                session_id=str(session_id),
                state=breakpoint_result.get("state") or attach_result.get("state"),
                summary="Delayed attach completed with breakpoint failures",
                error=breakpoint_result.get("error")
                or _error("breakpoint_unresolved", "one or more breakpoints failed", recoverable=True),
                data=data,
                diagnostics=list(attach_result.get("diagnostics") or []),
            )

    return _envelope(
        ok=True,
        session_id=str(session_id),
        state=attach_result.get("state"),
        summary=f"Launched process {launched_pid}, delayed {attach_delay_ms}ms, attached CDB",
        data=data,
        diagnostics=list(attach_result.get("diagnostics") or []),
    )


def debug_session_stop(
    session_id: str,
    *,
    kill_process: bool = True,
    detach: bool = False,
) -> dict[str, Any]:
    session, error = _session_or_error(session_id)
    if error is not None:
        return error
    assert session is not None
    result = session.stop(kill_process=False if detach else kill_process)
    with _SESSION_LOCK:
        _SESSIONS.pop(session_id, None)
    return _envelope(
        ok=True,
        session_id=session_id,
        state=result["final_state"],
        summary="Debug session stopped",
        data={**result, "detach": detach},
        diagnostics=list(session.diagnostics),
    )


def debug_session_status(session_id: str) -> dict[str, Any]:
    session, error = _session_or_error(session_id)
    if error is not None:
        return error
    assert session is not None
    return _envelope(
        ok=True,
        session_id=session_id,
        state=session.state,
        summary=f"Session is {session.state}",
        data={
            "ready": session.ready,
            "last_stop_reason": (session.last_stop_event or {}).get("reason"),
            "last_stop_event": session.last_stop_event,
            "current_thread": session.current_thread,
            "current_frame": session.current_frame,
            "process_id": session.process_id,
            "inferior_pid": session.inferior_pid,
            "cdb_pid": session.cdb_pid,
            "attached": session.attached,
            "exe": session.exe_path,
            "exit_code": session.exit_code,
            "current_operation": dict(session.current_operation) if session.current_operation else None,
            "operation_log_tail": list(session.operation_log[-5:]),
            "created_at": session.created_at,
            "last_activity_at": session.last_activity_at,
            "last_output_at": session.last_output_at,
        },
        diagnostics=list(session.diagnostics),
    )


def debug_session_wait_ready(session_id: str, *, timeout_ms: int = DEFAULT_STARTUP_TIMEOUT_MS) -> dict[str, Any]:
    session, error = _session_or_error(session_id)
    if error is not None:
        return error
    assert session is not None
    if session.ready:
        return _envelope(
            ok=True,
            session_id=session_id,
            state=session.state,
            summary="Session already ready",
            data={"ready": True},
            diagnostics=list(session.diagnostics),
        )
    operation = session.wait_ready(timeout_ms=timeout_ms)
    if operation.status == "timeout":
        return _envelope(
            ok=False,
            session_id=session_id,
            state=session.state,
            summary="Startup still in progress",
            error=_error(
                "timeout",
                operation.error_message or "startup not ready within timeout",
                recoverable=True,
                suggested_next_actions=[
                    "Retry debug_session_wait_ready with larger timeout_ms.",
                    "Call debug_session_stop to cancel.",
                ],
            ),
            data={"ready": False, "operation": operation.as_payload()},
            diagnostics=list(session.diagnostics),
        )
    if operation.status != "completed":
        return _envelope(
            ok=False,
            session_id=session_id,
            state=session.state,
            summary="Failed to wait for session ready",
            error=_error(
                operation.status,
                operation.error_message or "startup failed",
                recoverable=False,
                suggested_next_actions=["Call debug_session_stop and start a new session."],
            ),
            data={"ready": False, "operation": operation.as_payload()},
            diagnostics=list(session.diagnostics),
        )
    return _envelope(
        ok=True,
        session_id=session_id,
        state=session.state,
        summary="Session is ready",
        data={
            "ready": True,
            "last_stop_event": session.last_stop_event,
            "operation": operation.as_payload(),
        },
        diagnostics=list(session.diagnostics),
    )


def debug_session_list() -> dict[str, Any]:
    with _SESSION_LOCK:
        active = {session.session_id: session.snapshot() for session in _SESSIONS.values()}
    persisted = _read_session_registry()
    sessions = list(active.values())
    stale_session_ids: list[str] = []
    for session_id, meta in persisted.items():
        if session_id not in active:
            orphan = _registry_snapshot_entry(meta)
            if orphan["cdb_alive"] or orphan["inferior_alive"]:
                sessions.append(orphan)
            else:
                stale_session_ids.append(session_id)
    for stale_session_id in stale_session_ids:
        _remove_persisted_session(stale_session_id)
    return _envelope(
        ok=True,
        session_id=None,
        state=None,
        summary=f"{len(sessions)} active debug session(s)",
        data={"sessions": sessions, "session_count": len(sessions)},
    )


def debug_session_cleanup(*, session_id: str = "", force: bool = True) -> dict[str, Any]:
    cleaned: list[dict[str, Any]] = []
    persisted_to_clean: list[dict[str, Any]] = []
    with _SESSION_LOCK:
        targets = (
            {session_id: _SESSIONS[session_id]}
            if session_id and session_id in _SESSIONS
            else dict(_SESSIONS)
        )
        for sid, session in list(targets.items()):
            result = session.stop(kill_process=force)
            cleaned.append({"session_id": sid, **result})
            _SESSIONS.pop(sid, None)
    persisted = _read_session_registry()
    if session_id:
        if session_id in persisted and session_id not in targets:
            persisted_to_clean.append(persisted[session_id])
        elif session_id not in persisted and session_id not in targets:
            return _envelope(
                ok=False,
                session_id=session_id,
                state=None,
                summary="Session not found for cleanup",
                error=_error(
                    "session_not_found",
                    f"Unknown session_id: {session_id}",
                    recoverable=False,
                    suggested_next_actions=["Call debug_session_list to inspect active sessions."],
                ),
            )
    else:
        for sid, meta in persisted.items():
            if sid not in targets:
                persisted_to_clean.append(meta)
    for meta in persisted_to_clean:
        orphan_result = _cleanup_registry_entry(meta, force=force)
        cleaned.append(orphan_result)
        _remove_persisted_session(str(meta.get("session_id") or ""))
    return _envelope(
        ok=True,
        session_id=session_id or None,
        state=None,
        summary=f"Cleaned {len(cleaned)} debug session(s)",
        data={"cleaned": cleaned, "force": force, "orphan_cleanup_count": len(persisted_to_clean)},
    )


def debug_thread_select(session_id: str, thread_id: int) -> dict[str, Any]:
    def _select(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute([f"~{thread_id}s"], wait_for_stop=False))
        session.current_thread = thread_id
        return {"current_thread": thread_id, "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Selected thread", _select)


def debug_breakpoint_set_bulk(
    session_id: str,
    breakpoints: list[dict[str, Any]],
) -> dict[str, Any]:
    results: list[dict[str, Any]] = []
    failures = 0
    for spec in breakpoints:
        one = debug_breakpoint_set(
            session_id,
            source_file=str(spec.get("source_file", "")),
            line=int(spec.get("line", 0) or 0),
            symbol=str(spec.get("symbol", "")),
            address=str(spec.get("address", "")),
            condition=str(spec.get("condition", "")),
            hit_count=int(spec.get("hit_count", 0) or 0),
            module=str(spec.get("module", "")),
        )
        results.append(one)
        if not one.get("ok"):
            failures += 1
    state = None
    with _SESSION_LOCK:
        session = _SESSIONS.get(session_id)
        if session is not None:
            state = session.state
    return _envelope(
        ok=failures == 0,
        session_id=session_id,
        state=state,
        summary=f"Set {len(results) - failures}/{len(results)} breakpoints",
        data={"results": results, "failure_count": failures},
        error=None
        if failures == 0
        else _error(
            "breakpoint_unresolved",
            f"{failures} breakpoint(s) failed",
            recoverable=True,
            suggested_next_actions=["Inspect data.results[].error and binding_diagnostics."],
        ),
    )


def debug_contract_get() -> dict[str, Any]:
    return _envelope(
        ok=True,
        session_id=None,
        state=None,
        summary="Debugger MCP contract V6 (CDB-first diagnosis + dump/PDB/modules/heap/watchpoints)",
        data={
            "contract_version": DEBUGGER_MCP_CONTRACT_VERSION,
            "extended_tools": [
                "debug_modules_get",
                "debug_pdb_resolve",
                "debug_crash_dump_analyze",
                "debug_heap_stat",
                "debug_watchpoint_set",
            ],
            "diagnostic_schema_version": DEBUG_DIAGNOSTIC_SCHEMA_VERSION,
            "agent_workflow_schema_version": DEBUG_AGENT_WORKFLOW_SCHEMA_VERSION,
            "evidence_source_policy": {
                "root_cause": "cdb_primary",
                "failure_bundle": "bundle_route_only",
                "raw_log": "log_fallback_only",
                "principle": "logs route, CDB proves",
            },
            "agent_actions": [
                "diagnose",
                "inspect_only",
                "need_more_evidence",
                "not_reproduced",
                "environment_blocked",
            ],
            "debugger_probe_questions": [
                "what_failed",
                "where_did_cdb_stop",
                "which_frame_is_root",
                "which_locals_matter",
                "what_probe_next",
            ],
            "session_states": sorted(SESSION_STATES),
            "error_kinds": sorted(ERROR_KINDS),
            "stop_reasons": sorted(STOP_REASONS),
            "binding_statuses": sorted(BINDING_STATUSES),
            "operation_statuses": sorted(OPERATION_STATUSES),
            "defaults": {
                "timeout_ms": DEFAULT_TIMEOUT_MS,
                "startup_timeout_ms": DEFAULT_STARTUP_TIMEOUT_MS,
                "max_memory_read": MAX_MEMORY_READ,
            },
        },
    )


def debug_self_check() -> dict[str, Any]:
    root = repo_root()
    cdb_info = find_cdb()
    resolution = resolve_exe(root)
    exe_exists = resolution.exe.is_file()
    pdb_exists = resolution.exe.with_suffix(".pdb").is_file()
    llvm_pdbutil = shutil.which("llvm-pdbutil") or shutil.which("llvm-pdbutil.exe")
    issues: list[str] = []
    if not cdb_info["found"]:
        issues.append("cdb.exe not found")
    if not exe_exists:
        issues.append(f"default exe missing: {resolution.exe}")
    if exe_exists and not pdb_exists:
        issues.append(f"default PDB missing: {resolution.exe.with_suffix('.pdb')}")
    if not llvm_pdbutil:
        issues.append("llvm-pdbutil not on PATH (source breakpoint PDB fallback degraded)")
    ready = not issues or (cdb_info["found"] and exe_exists)
    return _envelope(
        ok=ready,
        session_id=None,
        state=None,
        summary="Debugger environment ready" if ready else "Debugger environment has issues",
        data={
            "cdb": cdb_info,
            "resolved_exe": resolution.as_payload(),
            "exe_exists": exe_exists,
            "pdb_exists": pdb_exists,
            "llvm_pdbutil": llvm_pdbutil,
            "issues": issues,
            "contract_version": DEBUGGER_MCP_CONTRACT_VERSION,
        },
        error=None
        if ready
        else _error(
            "launch_failed",
            "; ".join(issues),
            recoverable=True,
            suggested_next_actions=[
                "Build the target (e.g. cmake --build --preset debug --target EpicMapEditor).",
                "Set CRASH_ANALYSIS_DEBUGGER_PATH to cdb.exe.",
                "Call debug_resolve_cdb.",
            ],
        ),
    )


def debug_breakpoint_set(
    session_id: str,
    *,
    source_file: str = "",
    line: int = 0,
    symbol: str = "",
    address: str = "",
    condition: str = "",
    hit_count: int = 0,
    thread_id: int | None = None,
    module: str = "",
) -> dict[str, Any]:
    def _set(session: CdbSession) -> dict[str, Any]:
        resolved_from_pdb: dict[str, Any] | None = None
        binding_command = ""
        if source_file and line > 0:
            location = f"{_cdb_path(source_file)}:{line}"
            resolved_from_pdb = resolve_source_line_address(session, source_file, line)
            if resolved_from_pdb is not None:
                cmd = f"bp {resolved_from_pdb['address']}"
            else:
                cmd = f"bu `{_cdb_path(source_file)}`:{line}"
            binding_command = cmd
        elif symbol:
            target = f"{module}!{symbol}" if module else symbol
            cmd = f"bu {target}"
            binding_command = cmd
        elif address:
            cmd = f"bu {address}"
            binding_command = cmd
        else:
            raise ValueError("Provide source_file+line, symbol, or address")
        if condition:
            cmd += f' ".if ({condition}) {{ .echo hit }} .else {gc}"'
        if hit_count > 0:
            cmd += f" ; .echo hit_count_not_supported_in_v1"
        text = _require_completed(session.execute([cmd, "bl"], wait_for_stop=False))
        parsed = parse_breakpoint_list(text)
        if not parsed:
            raise RuntimeError(f"Breakpoint was not created: {_truncate(text, 500)[0]}")
        bp = max(parsed, key=lambda item: item["breakpoint_id"])
        session.breakpoints[bp["breakpoint_id"]] = bp
        binding_diagnostics = _breakpoint_binding_diagnostics(
            source_file=source_file,
            line=line,
            symbol=symbol,
            module=module,
            address=address,
            resolved_from_pdb=resolved_from_pdb,
            direct_command=binding_command,
            bp=bp,
        )
        if hit_count > 0:
            binding_diagnostics["binding_status"] = "unsupported"
            binding_diagnostics["unsupported_reason"] = "hit_count is not supported in V2"
        binding_status = binding_diagnostics["binding_status"]
        resolved_location = bp["resolved_location"]
        if resolved_from_pdb is not None and source_file and line > 0:
            resolved_location = f"{_cdb_path(source_file)}:{line}"
        if binding_status == "failed":
            raise RuntimeError(
                f"Breakpoint binding failed: {binding_diagnostics.get('request')}"
            )
        return {
            "breakpoint_id": bp["breakpoint_id"],
            "resolved": binding_status == "bound",
            "binding_status": binding_status,
            "resolved_location": resolved_location,
            "enabled": bp["enabled"],
            "address": bp["address"],
            "symbolized": binding_status == "bound",
            "resolved_from_pdb": resolved_from_pdb,
            "binding_diagnostics": binding_diagnostics,
            "raw": _truncate(text)[0],
        }

    return _wrap_session_call(session_id, "Breakpoint set", _set)


def debug_breakpoint_list(session_id: str) -> dict[str, Any]:
    def _list(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute(["bl"], wait_for_stop=False))
        breakpoints = parse_breakpoint_list(text)
        for bp in breakpoints:
            session.breakpoints[bp["breakpoint_id"]] = bp
        return {"breakpoints": breakpoints, "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Listed breakpoints", _list)


def debug_breakpoint_remove(session_id: str, breakpoint_id: int) -> dict[str, Any]:
    def _remove(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute([f"bc {breakpoint_id}", "bl"], wait_for_stop=False))
        session.breakpoints.pop(breakpoint_id, None)
        return {"removed": breakpoint_id, "breakpoints": parse_breakpoint_list(text)}

    return _wrap_session_call(session_id, "Removed breakpoint", _remove)


def debug_breakpoint_enable(session_id: str, breakpoint_id: int) -> dict[str, Any]:
    def _enable(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute([f"be {breakpoint_id}", "bl"], wait_for_stop=False))
        return {"breakpoint_id": breakpoint_id, "breakpoints": parse_breakpoint_list(text)}

    return _wrap_session_call(session_id, "Enabled breakpoint", _enable)


def debug_breakpoint_disable(session_id: str, breakpoint_id: int) -> dict[str, Any]:
    def _disable(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute([f"bd {breakpoint_id}", "bl"], wait_for_stop=False))
        return {"breakpoint_id": breakpoint_id, "breakpoints": parse_breakpoint_list(text)}

    return _wrap_session_call(session_id, "Disabled breakpoint", _disable)


def debug_run_until_stop(session_id: str, *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
    def _run(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute(["g"], timeout_ms=timeout_ms, wait_for_stop=True))
        stop_event = parse_stop_event(text)
        session.last_stop_event = stop_event
        return {"stop_event": stop_event, "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Ran until stop", _run)


def debug_continue(session_id: str, *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
    return debug_run_until_stop(session_id, timeout_ms=timeout_ms)


def debug_step_over(session_id: str, *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
    def _step(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute(["p"], timeout_ms=timeout_ms, wait_for_stop=True))
        stop_event = parse_stop_event(text)
        stop_event["reason"] = "step_complete"
        session.last_stop_event = stop_event
        return {"stop_event": stop_event, "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Stepped over", _step)


def debug_step_into(session_id: str, *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
    def _step(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute(["t"], timeout_ms=timeout_ms, wait_for_stop=True))
        stop_event = parse_stop_event(text)
        stop_event["reason"] = "step_complete"
        session.last_stop_event = stop_event
        return {"stop_event": stop_event, "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Stepped into", _step)


def debug_step_out(session_id: str, *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
    def _step(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute(["gu"], timeout_ms=timeout_ms, wait_for_stop=True))
        stop_event = parse_stop_event(text)
        stop_event["reason"] = "step_complete"
        session.last_stop_event = stop_event
        return {"stop_event": stop_event, "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Stepped out", _step)


def debug_interrupt(session_id: str) -> dict[str, Any]:
    def _interrupt(session: CdbSession) -> dict[str, Any]:
        return session.interrupt(timeout_ms=DEFAULT_TIMEOUT_MS)

    return _wrap_session_call(session_id, "Interrupted debuggee", _interrupt)


def debug_stack_get(session_id: str) -> dict[str, Any]:
    def _stack(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute(["kL"], wait_for_stop=False))
        frames = parse_stack_frames(text)
        return {"frames": frames, "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Captured stack", _stack)


def debug_frame_select(session_id: str, frame: int) -> dict[str, Any]:
    def _select(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute([f".frame {frame}"], wait_for_stop=False))
        session.current_frame = frame
        return {"current_frame": frame, "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Selected frame", _select)


def debug_scopes_get(session_id: str, *, name_filter: str = "") -> dict[str, Any]:
    def _scopes(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute(["dv"], wait_for_stop=False))
        locals_ = parse_locals(text)
        filtered = _filter_locals_by_name(locals_, name_filter)
        return {
            "locals": filtered,
            "args": [],
            "filter_applied": bool(name_filter.strip()),
            "name_filter": name_filter,
            "matched_count": len(filtered),
            "raw": _truncate(text)[0],
        }

    return _wrap_session_call(session_id, "Captured locals", _scopes)


def debug_expression_eval(
    session_id: str,
    expression: str,
    *,
    frame: int | None = None,
    thread_id: int | None = None,
) -> dict[str, Any]:
    if not expression:
        return _envelope(
            ok=False,
            session_id=session_id,
            state=None,
            summary="Expression is required",
            error=_error(
                "invalid_expression",
                "expression must be non-empty",
                recoverable=True,
                suggested_next_actions=["Pass a debugger expression such as a variable or field path."],
            ),
        )

    def _eval(session: CdbSession) -> dict[str, Any]:
        commands: list[str] = []
        if thread_id is not None:
            commands.append(f"~{thread_id}s")
            session.current_thread = thread_id
        if frame is not None:
            commands.append(f".frame {frame}")
            session.current_frame = frame
        eval_commands = [*commands, f"?? {expression}"]
        text = _require_completed(session.execute(eval_commands, wait_for_stop=False))
        result = parse_expression_eval(text)
        if result.get("error"):
            locals_text = _require_completed(session.execute([*commands, "dv"], wait_for_stop=False))
            fallback = _fallback_expression_eval_from_locals(locals_text, expression)
            if fallback is None:
                raise ValueError(str(result["error"]))
            return {
                "expression": expression,
                **fallback,
                "raw": _truncate(text)[0],
                "fallback_raw": _truncate(locals_text)[0],
            }
        return {
            "expression": expression,
            **result,
            "evaluation_mode": "cxx_expr",
            "raw": _truncate(text)[0],
        }

    result = _wrap_session_call(session_id, "Evaluated expression", _eval)
    if not result.get("ok") and result.get("error", {}).get("kind") == "backend_error":
        result["error"] = _error(
            "invalid_expression",
            result["error"]["message"],
            recoverable=True,
            suggested_next_actions=[
                "Select the correct frame with debug_frame_select.",
                "Use debug_memory_read for raw memory.",
            ],
        )
    return result


def debug_memory_read(
    session_id: str,
    address: str,
    size: int,
    *,
    format: str = "bytes",
) -> dict[str, Any]:
    if not address:
        return _envelope(
            ok=False,
            session_id=session_id,
            state=None,
            summary="Address is required",
            error=_error("invalid_input", "address is required", recoverable=True),
        )
    size = max(1, min(size, MAX_MEMORY_READ))

    def _read(session: CdbSession) -> dict[str, Any]:
        cmd = {
            "bytes": f"db {address} L{size}",
            "dwords": f"dd {address} L{max(1, size // 4)}",
            "qwords": f"dq {address} L{max(1, size // 8)}",
        }.get(format, f"db {address} L{size}")
        text = _require_completed(session.execute([cmd], wait_for_stop=False))
        parsed = parse_memory_read(text)
        return {"address": address, "size": size, "format": format, **parsed, "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Read memory", _read)


def debug_registers_get(session_id: str) -> dict[str, Any]:
    def _regs(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute(["r"], wait_for_stop=False))
        return {"registers": parse_registers(text), "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Captured registers", _regs)


def debug_threads_get(session_id: str) -> dict[str, Any]:
    def _threads(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute(["~"], wait_for_stop=False))
        return {"threads": parse_threads(text), "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Captured threads", _threads)


def run_cdb_batch(
    commands: list[str],
    *,
    dump_path: str = "",
    exe_path: str = "",
    args: list[str] | None = None,
    cwd: str | None = None,
    symbols: list[str] | None = None,
    timeout_ms: int = DEFAULT_TIMEOUT_MS,
) -> dict[str, Any]:
    cdb_info = find_cdb()
    if not cdb_info["found"]:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="cdb.exe not found",
            error=_error(
                "launch_failed",
                "cdb.exe not found",
                recoverable=True,
                suggested_next_actions=["debug_resolve_cdb", "debug_self_check"],
            ),
        )

    dump = dump_path.strip()
    exe = exe_path.strip()
    if dump:
        dump_file = Path(dump)
        if not dump_file.is_absolute():
            dump_file = (repo_root() / dump_file).resolve()
        if not dump_file.is_file():
            return _envelope(
                ok=False,
                session_id=None,
                state=None,
                summary="Crash dump not found",
                error=_error("dump_not_found", f"Missing dump: {dump_file}", recoverable=True),
            )
        cmd = [cdb_info["path"], "-z", str(dump_file), "-c", "; ".join([*commands, "q"])]
    elif exe:
        resolved_exe = str(_resolve_exe(exe, cwd))
        cmd = [cdb_info["path"], "-c", "; ".join([*commands, "q"]), resolved_exe, *(args or [])]
    else:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Provide dump_path or exe_path",
            error=_error("invalid_input", "dump_path or exe_path is required", recoverable=True),
        )

    workdir = str(Path(cwd).resolve()) if cwd else str(repo_root())
    if symbols:
        prefix = [".symfix", ".reload"]
        for sym_path in symbols:
            path = _cdb_path(sym_path).strip()
            if path:
                prefix.append(f".sympath+ {path}")
        if dump:
            cmd[3] = "; ".join([*prefix, *commands, "q"])
        else:
            cmd[3] = "; ".join([*prefix, *commands, "q"])

    try:
        proc = subprocess.run(
            cmd,
            cwd=workdir,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=max(1, timeout_ms / 1000.0),
            check=False,
        )
    except subprocess.TimeoutExpired:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="cdb batch timed out",
            error=_error("timeout", f"Exceeded {timeout_ms}ms", recoverable=True),
        )

    combined = (proc.stdout or "") + (proc.stderr or "")
    preview, truncated = _truncate(combined)
    return _envelope(
        ok=proc.returncode == 0,
        session_id=None,
        state=None,
        summary="cdb batch completed" if proc.returncode == 0 else "cdb batch failed",
        data={
            "exit_code": proc.returncode,
            "command": " ".join(cmd),
            "raw_preview": preview,
            "raw_truncated": truncated,
        },
    )


def debug_modules_get(session_id: str, *, filter_text: str = "") -> dict[str, Any]:
    def _modules(session: CdbSession) -> dict[str, Any]:
        cmd = f"lm m {filter_text}" if filter_text.strip() else "lm"
        text = _require_completed(session.execute([cmd], wait_for_stop=False))
        modules = parse_modules_list(text)
        return {
            "modules": modules,
            "count": len(modules),
            "filter_text": filter_text,
            "raw": _truncate(text)[0],
        }

    return _wrap_session_call(session_id, "Listed modules", _modules)


def debug_pdb_resolve(
    *,
    symbol: str = "",
    source_file: str = "",
    line: int = 0,
    exe_target: str = "",
    exe_path: str = "",
    build_dir: str = "",
) -> dict[str, Any]:
    if not symbol and not (source_file and line > 0):
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="symbol or source_file+line required",
            error=_error("invalid_input", "Provide symbol or source_file+line", recoverable=True),
        )

    root = repo_root()
    if exe_path.strip():
        exe = Path(exe_path)
        if not exe.is_absolute():
            exe = (root / exe).resolve()
    else:
        resolution = resolve_exe(root, exe_target=exe_target or None)
        exe = resolution.exe

    if not exe.is_file():
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Executable not found",
            error=_error("launch_failed", f"Missing exe: {exe}", recoverable=True),
        )

    pdb = exe.with_suffix(".pdb")
    data: dict[str, Any] = {
        "exe": str(exe),
        "pdb": {"path": str(pdb), "exists": pdb.is_file()},
    }

    if source_file and line > 0:
        if pdb.is_file() and find_llvm_pdbutil():
            try:
                modules_text = _run_pdbutil(["dump", "--modules", "--files", str(pdb)])
                module_ids = _parse_pdb_modules_for_source(modules_text, source_file)
                line_records: list[dict[str, Any]] = []
                for module_id in module_ids:
                    lines_text = _run_pdbutil(["dump", "-l", f"--modi={module_id}", str(pdb)])
                    for section_index, offset in _parse_pdb_line_records(lines_text, source_file, line):
                        line_records.append(
                            {
                                "module_id": module_id,
                                "section_index": section_index,
                                "offset": offset,
                                "source_file": source_file,
                                "line": line,
                            }
                        )
                data["source_line_records"] = line_records
            except Exception as exc:
                data["pdbutil_error"] = str(exc)

        batch = run_cdb_batch(
            [".lines -e", f"ln `{_cdb_path(source_file)}`:{line}"],
            exe_path=str(exe),
            timeout_ms=60_000,
        )
        data["cdb_ln"] = batch.get("data", {})
        return _envelope(
            ok=batch.get("ok", False) or bool(data.get("source_line_records")),
            session_id=None,
            state=None,
            summary=f"PDB resolve for {source_file}:{line}",
            data=data,
        )

    batch = run_cdb_batch(
        [".lines -e", f"x {symbol}"],
        exe_path=str(exe),
        timeout_ms=60_000,
    )
    data["cdb_x"] = batch.get("data", {})
    return _envelope(
        ok=bool(batch.get("ok")),
        session_id=None,
        state=None,
        summary=f"PDB resolve for symbol {symbol}",
        data=data,
    )


def _first_neverwhere_frame(frames: list[dict[str, Any]]) -> dict[str, Any] | None:
    """First frame whose source file lives under the neverwhere repo root.

    Post-mortem stack tops are usually CRT/SEH trampolines; the first project
    frame is where the actual fault surfaced in user code.
    """
    try:
        root = repo_root().resolve()
    except Exception:
        return frames[0] if frames else None
    for frame in frames:
        source = str(frame.get("source") or frame.get("file") or "")
        if not source:
            continue
        try:
            if Path(source).resolve().is_relative_to(root):
                return frame
        except Exception:
            continue
    return frames[0] if frames else None


def debug_crash_dump_analyze(
    dump_path: str,
    *,
    symbols: str = "",
    top_frames: int = 24,
    timeout_ms: int = 120_000,
) -> dict[str, Any]:
    if not dump_path.strip():
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="dump_path required",
            error=_error("invalid_input", "dump_path is required", recoverable=True),
        )

    sym_list = [item.strip() for item in symbols.split(";") if item.strip()] if symbols else None
    batch = run_cdb_batch(
        ["k", "~"],
        dump_path=dump_path,
        symbols=sym_list,
        timeout_ms=timeout_ms,
    )
    if not batch.get("ok") and batch.get("error"):
        return batch

    preview = str((batch.get("data") or {}).get("raw_preview") or "")
    frames = parse_stack_frames(preview)[: max(1, top_frames)]
    exception_match = re.search(
        r"(ExceptionCode: [^\n]+|STATUS_[A-Z_0-9]+|access violation[^\n]*)",
        preview,
        re.I,
    )
    return _envelope(
        ok=True,
        session_id=None,
        state=None,
        summary=f"Crash dump analyzed ({len(frames)} frame(s))",
        data={
            "dump_path": dump_path,
            "exception": exception_match.group(1) if exception_match else None,
            "frames": frames,
            "first_project_frame": _first_neverwhere_frame(frames),
            "raw_preview": preview,
        },
        diagnostics=["Post-mortem analysis only; use debug_session_start for live repro"],
    )


def debug_heap_stat(session_id: str) -> dict[str, Any]:
    def _heap(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute(["!heap -s"], wait_for_stop=False))
        summary = parse_heap_summary(text)
        return {"heap_summary": summary, "raw": _truncate(text)[0]}

    return _wrap_session_call(session_id, "Captured heap summary", _heap)


def debug_watchpoint_set(
    session_id: str,
    *,
    address: str = "",
    expression: str = "",
    access: str = "write",
    size: int = 4,
) -> dict[str, Any]:
    target = address.strip()
    if not target and expression.strip():
        eval_result = debug_expression_eval(session_id, expression)
        if not eval_result.get("ok"):
            return eval_result
        value = (eval_result.get("data") or {}).get("value")
        target = str(value or "").strip()
    if not target:
        return _envelope(
            ok=False,
            session_id=session_id,
            state=None,
            summary="address or expression required",
            error=_error("invalid_input", "Provide address or expression", recoverable=True),
        )

    access_code = {"read": "r", "write": "w", "execute": "e"}.get(access.lower(), "w")
    size_code = {1: "1", 2: "2", 4: "4", 8: "8"}.get(max(1, min(size, 8)), "4")
    cmd = f"ba {access_code}{size_code} {target}"

    def _set(session: CdbSession) -> dict[str, Any]:
        text = _require_completed(session.execute([cmd, "bl"], wait_for_stop=False))
        breakpoints = parse_breakpoint_list(text)
        return {
            "watchpoint_command": cmd,
            "address": target,
            "access": access,
            "size": size,
            "breakpoints": breakpoints,
            "raw": _truncate(text)[0],
        }

    return _wrap_session_call(session_id, "Watchpoint set", _set)



def debug_resolve_cdb(custom_path: str = "") -> dict[str, Any]:
    info = find_cdb(custom_path or None)
    return _envelope(
        ok=bool(info["found"]),
        session_id=None,
        state=None,
        summary="Resolved cdb path" if info["found"] else "cdb.exe not found",
        data={"cdb": info},
        error=None
        if info["found"]
        else _error(
            "launch_failed",
            "cdb.exe not found",
            recoverable=False,
            suggested_next_actions=["Run vcpkg install windows-debuggers.", "Set CRASH_ANALYSIS_DEBUGGER_PATH."],
        ),
    )


def debug_process_find(name_query: str, *, limit: int = 20) -> dict[str, Any]:
    query = name_query.strip()
    if not query:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Process name query is required",
            error=_error(
                "invalid_input",
                "name_query must be non-empty",
                recoverable=True,
                suggested_next_actions=["Pass a process name fragment such as EpicMapEditor or PolygonalGeneratedLandscapePlayground."],
            ),
        )
    try:
        matches = find_processes_by_name(query, limit=limit)
    except Exception as exc:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Failed to enumerate running processes",
            error=_error(
                "backend_error",
                str(exc),
                recoverable=True,
                suggested_next_actions=["Retry the query.", "Check whether tasklist works in this environment."],
            ),
        )
    return _envelope(
        ok=True,
        session_id=None,
        state=None,
        summary=f"Found {len(matches)} matching processes" if matches else "No matching processes found",
        data={
            "query": query,
            "matches": matches,
            "match_count": len(matches),
        },
    )


def debug_process_terminate(*, process_id: int, expected_name: str) -> dict[str, Any]:
    expected_normalized = _normalize_process_name(expected_name)
    if process_id <= 0 or not expected_normalized:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Process id and exact expected name are required",
            error=_error(
                "invalid_input",
                "process_id must be positive and expected_name must be non-empty",
                recoverable=True,
                suggested_next_actions=[
                    "Call debug_process_find to obtain a process id and exact image name.",
                ],
            ),
        )
    try:
        process = next(
            (item for item in _list_processes() if int(item["process_id"]) == process_id),
            None,
        )
    except Exception as exc:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Unable to inspect target process",
            error=_error("process_query_failed", str(exc), recoverable=True),
        )
    if process is None:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Target process is no longer running",
            error=_error(
                "process_not_found",
                f"No process with id {process_id}",
                recoverable=True,
                suggested_next_actions=["Call debug_process_find again before retrying."],
            ),
        )
    if process["process_name_normalized"] != expected_normalized:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Target process name did not match",
            error=_error(
                "process_name_mismatch",
                f"Process {process_id} is '{process['process_name']}', not '{expected_name}'",
                recoverable=False,
            ),
            data={"process": process},
        )

    terminated = _kill_process_tree(process_id)
    if not terminated or _process_exists(process_id):
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Failed to terminate target process",
            error=_error(
                "terminate_failed",
                f"Unable to terminate process {process_id} ({process['process_name']})",
                recoverable=True,
                suggested_next_actions=["Check whether another debugger or elevated process owns the target."],
            ),
            data={"process": process, "terminated": terminated},
        )
    return _envelope(
        ok=True,
        session_id=None,
        state=None,
        summary="Terminated target process",
        data={"process": process, "terminated": True},
    )


def debug_variable_expand(
    session_id: str,
    var_path: str = "",
    *,
    frame: int | None = None,
    max_children: int = 64,
    depth: int = 1,
) -> dict[str, Any]:
    """Variable drill-down. Only implemented on the macOS LLDB backend."""
    return _envelope(
        ok=False,
        session_id=session_id,
        state=None,
        summary="debug_variable_expand is not supported on the Windows cdb backend",
        error=_error(
            "not_supported",
            "debug_variable_expand is implemented by the macOS LLDB backend only",
            recoverable=False,
        ),
    )


def debug_crash_report(
    session_id: str,
    *,
    frames_per_thread: int = 32,
    log_tail_lines: int = 60,
) -> dict[str, Any]:
    """One-shot crash snapshot. Only implemented on the macOS LLDB backend."""
    return _envelope(
        ok=False,
        session_id=session_id,
        state=None,
        summary="debug_crash_report is not supported on the Windows cdb backend",
        error=_error(
            "not_supported",
            "debug_crash_report is implemented by the macOS LLDB backend only",
            recoverable=False,
        ),
    )


def reset_sessions_for_tests() -> None:
    with _SESSION_LOCK:
        for session in list(_SESSIONS.values()):
            session.stop(kill_process=True)
        _SESSIONS.clear()
    persisted = _read_session_registry()
    for meta in persisted.values():
        _cleanup_registry_entry(meta, force=True)
    path = _session_registry_path()
    if path.is_file():
        try:
            path.unlink()
        except OSError:
            _write_session_registry({})
