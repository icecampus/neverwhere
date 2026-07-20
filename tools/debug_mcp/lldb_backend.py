"""macOS LLDB backend for the neverwhere debug MCP server.

Pure-function mirror of ``debugger_mcp.py`` (the Windows cdb backend): the
same tool names, the same envelope shape (``ok`` / ``session_id`` / ``state``
/ ``summary`` / ``data`` / ``error`` / ``diagnostics`` / ``backend``), the
same error kinds.

The ``lldb`` SB module is only available to the system ``/usr/bin/python3``
(Xcode ships it for cp39, see ``xcrun lldb -P``), while the MCP SDK needs
Python >= 3.10. This module therefore spawns one long-lived worker process
(``lldb_worker.py`` under ``/usr/bin/python3``) that owns the live
SBDebugger/SBTarget/SBProcess objects, and exchanges JSON-lines requests
with it (``{"id", "cmd", "args"}`` -> ``{"id", "ok", "result"/"error"}``).
The worker is restarted automatically when it dies or stops answering.
"""

from __future__ import annotations

import json
import os
import re
import shutil
import signal
import subprocess
import threading
import time
import uuid
from pathlib import Path
from typing import Any

from tools.debug_mcp.exe_resolver import resolve_exe
from tools.debug_mcp.repo_root import repo_root

DEBUGGER_MCP_CONTRACT_VERSION = 6
DEBUG_DIAGNOSTIC_SCHEMA_VERSION = 4
DEBUG_AGENT_WORKFLOW_SCHEMA_VERSION = 4
MAX_MEMORY_READ = 4096
DEFAULT_TIMEOUT_MS = 30_000
DEFAULT_STARTUP_TIMEOUT_MS = 30_000
# Slack added to run-control timeouts so the worker always answers first.
RUN_CONTROL_SLACK_MS = 15_000
WORKER_HANDSHAKE_TIMEOUT_MS = 20_000

SESSION_STATES = frozenset({"launching", "running", "stopped", "exited", "error"})
BINDING_STATUSES = frozenset({"bound", "pending_deferred", "failed", "ambiguous", "unsupported"})
OPERATION_STATUSES = frozenset({"completed", "timeout", "process_exited", "backend_error"})

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
        "not_supported",
        "variable_not_found",
        "invalid_path",
    }
)

STOP_REASONS = frozenset(
    {
        "breakpoint",
        "watchpoint",
        "signal",
        "exception",
        "exited",
        "plan-complete",
        "step_complete",
        "manual_interrupt",
        "initial_stop",
        "none",
    }
)

# Error kinds where retrying with different input / more time is sensible.
_RECOVERABLE_KINDS = frozenset(
    {
        "timeout",
        "invalid_input",
        "invalid_expression",
        "invalid_state",
        "process_exited",
        "variable_not_found",
        "invalid_path",
    }
)

_SUGGESTED_NEXT = {
    "session_not_found": ["Call debug_session_start to create a new session."],
    "timeout": [
        "Retry with a larger timeout_ms.",
        "Call debug_interrupt.",
        "Call debug_session_status.",
        "Call debug_session_stop if recovery fails.",
    ],
    "invalid_expression": [
        "Select the correct frame with debug_frame_select.",
        "Use debug_memory_read for raw memory.",
    ],
    "invalid_state": ["Call debug_session_status to inspect the process state."],
    "variable_not_found": [
        "Call debug_scopes_get to list variables in the current frame.",
        "Check the frame index with debug_stack_get / debug_frame_select.",
    ],
    "invalid_path": [
        "Use paths like 'tile.assetUuid', 'this->layers', '(*this).map'.",
        "Call debug_scopes_get to list top-level variables.",
    ],
    "launch_failed": [
        "Build the target first (e.g. cmake --build --preset macos-debug --target EpicMapEditor).",
        "Pass exe or exe_target explicitly.",
    ],
}


# ---------------------------------------------------------------------------
# Envelope helpers (same shape as the cdb backend)
# ---------------------------------------------------------------------------


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
        "backend": "lldb",
    }


def _not_supported(tool: str, detail: str) -> dict[str, Any]:
    return _envelope(
        ok=False,
        session_id=None,
        state=None,
        summary=f"{tool} is not supported on the macOS LLDB backend",
        error=_error("not_supported", detail, recoverable=False),
    )


# ---------------------------------------------------------------------------
# Worker process management
# ---------------------------------------------------------------------------


class _CallError(RuntimeError):
    """A worker-side error with a contract ``kind``."""

    def __init__(self, kind: str, message: str):
        super().__init__(message)
        self.kind = kind
        self.message = message


class _WorkerGone(RuntimeError):
    pass


class _WorkerTimeout(RuntimeError):
    pass


def _lldb_python_path() -> str | None:
    try:
        result = subprocess.run(
            ["xcrun", "lldb", "-P"],
            capture_output=True,
            text=True,
            timeout=15,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return None
    path = (result.stdout or "").strip()
    return path if result.returncode == 0 and path else None


def find_lldb(custom_path: str | None = None) -> dict[str, Any]:
    """Locate the lldb CLI (for self-check display; the SB API does the work)."""
    if custom_path and os.path.isfile(custom_path):
        return {"path": os.path.abspath(custom_path), "source": "custom", "found": True}
    env_path = os.environ.get("CRASH_ANALYSIS_DEBUGGER_PATH", "").strip()
    if env_path and os.path.isfile(env_path):
        return {"path": os.path.abspath(env_path), "source": "env", "found": True}
    xcrun = shutil.which("xcrun")
    if xcrun:
        try:
            result = subprocess.run(
                ["xcrun", "-f", "lldb"],
                capture_output=True,
                text=True,
                timeout=15,
                check=False,
            )
            path = (result.stdout or "").strip()
            if result.returncode == 0 and path and os.path.isfile(path):
                return {"path": path, "source": "xcrun", "found": True}
        except (OSError, subprocess.TimeoutExpired):
            pass
    for candidate in ("/usr/bin/lldb", "/opt/homebrew/bin/lldb"):
        if os.path.isfile(candidate):
            return {"path": candidate, "source": "system", "found": True}
    return {"path": None, "source": None, "found": False}


class _Worker:
    """One ``lldb_worker.py`` subprocess with a reader thread."""

    WORKER_PYTHON = "/usr/bin/python3"

    def __init__(self) -> None:
        self.proc: subprocess.Popen[str] | None = None
        self.cond = threading.Condition()
        self.pending: dict[Any, dict[str, Any]] = {}
        self.eof = False
        self.next_id = 0
        self.stderr_tail: list[str] = []
        self.diagnostics: list[str] = []
        self.handshake: dict[str, Any] | None = None

    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None and not self.eof

    def start(self) -> None:
        worker_py = Path(__file__).resolve().with_name("lldb_worker.py")
        env = dict(os.environ)
        env["PYTHONIOENCODING"] = "utf-8"
        env["PYTHONUTF8"] = "1"
        lldb_python = _lldb_python_path()
        if lldb_python:
            existing = env.get("PYTHONPATH", "")
            env["PYTHONPATH"] = lldb_python + (os.pathsep + existing if existing else "")
        try:
            self.proc = subprocess.Popen(
                [self.WORKER_PYTHON, str(worker_py)],
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                encoding="utf-8",
                errors="replace",
                cwd=str(repo_root()),
                env=env,
                bufsize=1,
            )
        except OSError as exc:
            raise _WorkerGone(f"failed to spawn lldb worker: {exc}") from exc
        threading.Thread(target=self._stdout_reader, daemon=True).start()
        threading.Thread(target=self._stderr_reader, daemon=True).start()
        self.handshake = self.request("ping", {}, WORKER_HANDSHAKE_TIMEOUT_MS)

    def _stdout_reader(self) -> None:
        assert self.proc is not None and self.proc.stdout is not None
        for line in self.proc.stdout:
            text = line.strip()
            if not text:
                continue
            try:
                payload = json.loads(text)
            except json.JSONDecodeError:
                with self.cond:
                    self.diagnostics.append(f"non-json worker output: {text[:200]}")
                continue
            with self.cond:
                self.pending[payload.get("id")] = payload
                self.cond.notify_all()
        with self.cond:
            self.eof = True
            self.cond.notify_all()

    def _stderr_reader(self) -> None:
        assert self.proc is not None and self.proc.stderr is not None
        for line in self.proc.stderr:
            with self.cond:
                self.stderr_tail.append(line.rstrip()[:500])
                del self.stderr_tail[:-20]

    def request(self, cmd: str, args: dict[str, Any], timeout_ms: int) -> dict[str, Any]:
        if not self.alive():
            raise _WorkerGone("lldb worker process is not running")
        with self.cond:
            self.next_id += 1
            request_id = self.next_id
        line = json.dumps({"id": request_id, "cmd": cmd, "args": args}, ensure_ascii=False)
        try:
            assert self.proc is not None and self.proc.stdin is not None
            self.proc.stdin.write(line + "\n")
            self.proc.stdin.flush()
        except (BrokenPipeError, OSError) as exc:
            raise _WorkerGone(f"lldb worker stdin broke: {exc}") from exc
        deadline = time.monotonic() + max(1, timeout_ms) / 1000.0
        with self.cond:
            while request_id not in self.pending:
                if self.eof:
                    raise _WorkerGone("lldb worker exited without answering")
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise _WorkerTimeout(f"worker did not answer '{cmd}' within {timeout_ms} ms")
                self.cond.wait(timeout=min(remaining, 1.0))
            return self.pending.pop(request_id)

    def kill(self) -> None:
        proc = self.proc
        self.proc = None
        if proc is None:
            return
        try:
            proc.kill()
        except OSError:
            pass
        try:
            proc.wait(timeout=5)
        except (subprocess.TimeoutExpired, OSError):
            pass


_WORKER_LOCK = threading.Lock()
_WORKER: _Worker | None = None


def _ensure_worker() -> _Worker:
    global _WORKER
    with _WORKER_LOCK:
        if _WORKER is not None and _WORKER.alive():
            return _WORKER
        _WORKER = _Worker()
        try:
            _WORKER.start()
        except Exception:
            worker, _WORKER = _WORKER, None
            worker.kill()
            raise
        return _WORKER


def _reset_worker() -> None:
    global _WORKER
    with _WORKER_LOCK:
        worker, _WORKER = _WORKER, None
    if worker is not None:
        worker.kill()


def _call(cmd: str, args: dict[str, Any], *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
    """Send one command to the worker and unwrap the result."""
    global _WORKER
    try:
        with _WORKER_LOCK:
            worker = _WORKER if (_WORKER is not None and _WORKER.alive()) else None
        if worker is None:
            worker = _ensure_worker()
        payload = worker.request(cmd, args, timeout_ms)
    except _WorkerTimeout as exc:
        _reset_worker()
        raise _CallError(
            "timeout",
            f"{exc}; the lldb worker was restarted (live sessions were lost)",
        ) from exc
    except _WorkerGone as exc:
        with _WORKER_LOCK:
            old_worker = _WORKER
        hint = ""
        if old_worker is not None and old_worker.stderr_tail:
            hint = " | worker stderr: " + " ; ".join(old_worker.stderr_tail[-3:])
        _reset_worker()
        raise _CallError("backend_error", f"{exc}{hint}") from exc
    if payload.get("ok"):
        result = payload.get("result")
        return result if isinstance(result, dict) else {}
    err = payload.get("error") or {}
    raise _CallError(
        str(err.get("kind") or "backend_error"),
        str(err.get("message") or "lldb worker error"),
    )


def _wrap_worker_call(
    cmd: str,
    args: dict[str, Any],
    summary: str,
    *,
    timeout_ms: int = DEFAULT_TIMEOUT_MS,
    session_id: str | None = None,
) -> dict[str, Any]:
    try:
        result = _call(cmd, args, timeout_ms=timeout_ms)
    except _CallError as exc:
        return _envelope(
            ok=False,
            session_id=session_id or args.get("session_id"),
            state=None,
            summary=summary,
            error=_error(
                exc.kind if exc.kind in ERROR_KINDS else "backend_error",
                exc.message,
                recoverable=exc.kind in _RECOVERABLE_KINDS,
                suggested_next_actions=list(_SUGGESTED_NEXT.get(exc.kind, [])),
            ),
        )
    return _envelope(
        ok=True,
        session_id=str(result.get("session_id") or session_id or "") or None,
        state=result.get("state"),
        summary=summary,
        data=result,
        diagnostics=list(result.get("diagnostics") or []),
    )


# ---------------------------------------------------------------------------
# Process enumeration (macOS ps-based)
# ---------------------------------------------------------------------------


def _normalize_process_name(name: str) -> str:
    normalized = name.strip().casefold()
    if normalized.endswith(".exe"):
        normalized = normalized[:-4]
    if normalized.endswith(".app"):
        normalized = normalized[:-4]
    return normalized


def _list_processes() -> list[dict[str, Any]]:
    proc = subprocess.run(
        ["ps", "-axo", "pid=,comm="],
        stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=15,
        check=False,
    )
    if proc.returncode != 0:
        raise RuntimeError(f"ps failed: {(proc.stderr or '').strip()[:500]}")
    result: list[dict[str, Any]] = []
    for line in (proc.stdout or "").splitlines():
        line = line.strip()
        if not line:
            continue
        match = re.match(r"^(\d+)\s+(.*)$", line)
        if not match:
            continue
        pid = int(match.group(1))
        path = match.group(2).strip()
        name = os.path.basename(path)
        result.append(
            {
                "process_name": name,
                "process_name_normalized": _normalize_process_name(name),
                "process_path": path,
                "process_id": pid,
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


def find_processes_by_name(query: str, *, limit: int = 20) -> list[dict[str, Any]]:
    matches: list[dict[str, Any]] = []
    for process in _list_processes():
        score = _score_process_match(query, process["process_name"])
        if score <= 0:
            continue
        matches.append({**process, "score": score})
    matches.sort(key=lambda item: (-item["score"], item["process_id"]))
    return matches[: max(1, limit)]


def _resolve_attach_pid(process_id: int, process_name: str) -> tuple[int | None, dict[str, Any] | None]:
    """Resolve process_id/process_name to a concrete pid (cdb parity)."""
    if process_id > 0:
        return process_id, None
    query = process_name.strip()
    if not query:
        return None, None
    matches = find_processes_by_name(query, limit=5)
    if not matches:
        return None, {
            "kind": "launch_failed",
            "message": f"No running process matches '{query}'",
            "recoverable": True,
            "suggested_next_actions": ["Start the process, or launch it via debug_session_start."],
            "candidates": [],
        }
    best = matches[0]
    if len(matches) > 1 and matches[1]["score"] == best["score"]:
        return None, {
            "kind": "invalid_input",
            "message": f"Process name '{query}' is ambiguous",
            "recoverable": True,
            "suggested_next_actions": ["Pass process_id explicitly."],
            "candidates": matches,
        }
    return int(best["process_id"]), None


def _exe_path_for_pid(pid: int) -> str:
    try:
        proc = subprocess.run(
            ["ps", "-p", str(pid), "-o", "comm="],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return ""
    return (proc.stdout or "").strip()


# ---------------------------------------------------------------------------
# Environment / contract
# ---------------------------------------------------------------------------


def debug_resolve_cdb(custom_path: str = "") -> dict[str, Any]:
    """Name kept for contract parity; on macOS this locates lldb."""
    info = find_lldb(custom_path or None)
    return _envelope(
        ok=bool(info["found"]),
        session_id=None,
        state=None,
        summary="Resolved lldb path" if info["found"] else "lldb not found",
        data={"cdb": info, "lldb": info, "debugger_kind": "lldb"},
        error=None
        if info["found"]
        else _error(
            "launch_failed",
            "lldb not found (install Xcode / Command Line Tools)",
            recoverable=False,
            suggested_next_actions=["Install Xcode.", "Check xcrun -f lldb."],
        ),
    )


def debug_contract_get() -> dict[str, Any]:
    return _envelope(
        ok=True,
        session_id=None,
        state=None,
        summary="Debugger MCP contract V6 (macOS LLDB backend via SB API worker)",
        data={
            "contract_version": DEBUGGER_MCP_CONTRACT_VERSION,
            "backend_kind": "lldb",
            "extended_tools": [
                "debug_modules_get",
                "debug_pdb_resolve",
                "debug_crash_dump_analyze",
                "debug_heap_stat",
                "debug_watchpoint_set",
                "debug_variable_expand",
                "debug_crash_report",
            ],
            "not_supported_tools": [
                "debug_pdb_resolve",
                "debug_crash_dump_analyze",
                "debug_heap_stat",
            ],
            "diagnostic_schema_version": DEBUG_DIAGNOSTIC_SCHEMA_VERSION,
            "agent_workflow_schema_version": DEBUG_AGENT_WORKFLOW_SCHEMA_VERSION,
            "evidence_source_policy": {
                "root_cause": "lldb_primary",
                "failure_bundle": "bundle_route_only",
                "raw_log": "log_fallback_only",
                "principle": "logs route, LLDB proves",
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
                "where_did_lldb_stop",
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
    lldb_info = find_lldb()
    lldb_python = _lldb_python_path()
    worker_python_ok = os.path.isfile(_Worker.WORKER_PYTHON)
    resolution = resolve_exe(root)
    exe_exists = resolution.exe.is_file()
    issues: list[str] = []
    if not lldb_info["found"]:
        issues.append("lldb not found")
    if not lldb_python:
        issues.append("lldb python module path unavailable (xcrun lldb -P failed)")
    if not worker_python_ok:
        issues.append(f"worker python missing: {_Worker.WORKER_PYTHON}")
    if not exe_exists:
        issues.append(f"default exe missing: {resolution.exe}")
    worker_info: dict[str, Any] | None = None
    if not issues:
        try:
            worker_info = _call("self_check", {}, timeout_ms=WORKER_HANDSHAKE_TIMEOUT_MS)
        except _CallError as exc:
            issues.append(f"lldb worker handshake failed: {exc.message}")
    ready = not issues
    return _envelope(
        ok=ready,
        session_id=None,
        state=None,
        summary="Debugger environment ready" if ready else "Debugger environment has issues",
        data={
            "cdb": lldb_info,
            "lldb": lldb_info,
            "lldb_python_path": lldb_python,
            "worker_python": _Worker.WORKER_PYTHON,
            "worker": worker_info,
            "resolved_exe": resolution.as_payload(),
            "exe_exists": exe_exists,
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
                "Build the target (e.g. cmake --build --preset macos-debug --target EpicMapEditor).",
                "Check xcrun -f lldb and xcrun lldb -P.",
                "Call debug_resolve_cdb.",
            ],
        ),
    )


# ---------------------------------------------------------------------------
# Sessions
# ---------------------------------------------------------------------------


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
                    "Use only process_id or process_name (plus optional cwd/break_on_start) for attach.",
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
            error=_error("invalid_input", "process_id must be >= 0", recoverable=True),
        )

    resolved_process_id, attach_error = _resolve_attach_pid(process_id, process_name)
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
        if not resolution.exe.is_file():
            return _envelope(
                ok=False,
                session_id=None,
                state=None,
                summary="Executable not found",
                error=_error(
                    "launch_failed",
                    f"Executable not found: {resolution.exe}",
                    recoverable=True,
                    suggested_next_actions=[
                        "Build the target first (e.g. cmake --build --preset macos-debug --target EpicMapEditor).",
                        "Pass exe or exe_target explicitly.",
                    ],
                ),
                data={"resolved_exe": resolution.as_payload()},
            )
    else:
        resolved_exe = _exe_path_for_pid(resolved_process_id)

    diagnostics: list[str] = []
    if symbols or not reload_symbols:
        diagnostics.append("symbols/reload_symbols are ignored on macOS (DWARF is embedded in the binary)")

    try:
        result = _call(
            "session_start",
            {
                "exe": resolved_exe,
                "args": list(args or []),
                "cwd": workdir,
                "env": dict(env or {}),
                "process_id": resolved_process_id if attach_mode else 0,
                "break_on_start": break_on_start,
                "startup_timeout_ms": startup_timeout_ms,
                "repo_root": str(root),
            },
            timeout_ms=startup_timeout_ms + RUN_CONTROL_SLACK_MS,
        )
    except _CallError as exc:
        return _envelope(
            ok=False,
            session_id=None,
            state="error",
            summary="Failed to start debug session",
            error=_error(
                exc.kind if exc.kind in ERROR_KINDS else "launch_failed",
                exc.message,
                recoverable=exc.kind in _RECOVERABLE_KINDS,
                suggested_next_actions=list(_SUGGESTED_NEXT.get(exc.kind, _SUGGESTED_NEXT["launch_failed"])),
            ),
            data={
                "lldb": find_lldb(),
                "resolved_exe": resolution.as_payload() if resolution else None,
                "attach_process_id": resolved_process_id if attach_mode else None,
                "process_name_query": process_name or None,
            },
        )

    session_id = str(result.get("session_id") or "")
    diagnostics.extend(result.get("diagnostics") or [])
    return _envelope(
        ok=True,
        session_id=session_id,
        state=result.get("state"),
        summary=(
            f"Attached debug session to process {resolved_process_id}"
            if attach_mode
            else f"Started debug session for {resolved_exe}"
        ),
        data={
            "process_id": result.get("process_id"),
            "inferior_pid": result.get("process_id"),
            "attached": result.get("attached"),
            "ready": result.get("ready", True),
            "process_name_query": process_name or None,
            "backend": "lldb",
            "lldb": find_lldb(),
            "exe": resolved_exe,
            "resolved_exe": resolution.as_payload() if resolution else None,
            "args": list(args or []),
            "cwd": workdir,
            "break_on_start": break_on_start,
            "wait_ready": wait_ready,
            "startup_timeout_ms": startup_timeout_ms,
            "stdout_path": result.get("stdout_path"),
            "stderr_path": result.get("stderr_path"),
            "last_stop_event": result.get("last_stop_event"),
        },
        diagnostics=diagnostics,
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
    breakpoints: list[dict] | None = None,
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
    resolution = resolve_exe(
        root,
        exe_target=exe_target or None,
        exe_path=exe or None,
        config=config or None,
    )
    if not resolution.exe.is_file():
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Executable not found",
            error=_error(
                "launch_failed",
                f"Executable not found: {resolution.exe}",
                recoverable=True,
                suggested_next_actions=[
                    "Build the target first (e.g. cmake --build --preset macos-debug --target EpicMapEditor).",
                    "Pass exe or exe_target explicitly.",
                ],
            ),
        )
    workdir = cwd or str(resolution.cwd)
    merged_env = os.environ.copy()
    merged_env.update({str(k): str(v) for k, v in (env or {}).items()})
    log_dir = root / "test_logs"
    log_dir.mkdir(parents=True, exist_ok=True)
    launch_id = uuid.uuid4().hex
    stdout_path = log_dir / f"debug_launch_then_attach_{launch_id}.stdout.log"
    stderr_path = log_dir / f"debug_launch_then_attach_{launch_id}.stderr.log"
    try:
        with stdout_path.open("w", encoding="utf-8") as stdout_handle, stderr_path.open(
            "w", encoding="utf-8"
        ) as stderr_handle:
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
            error=_error("launch_failed", str(exc), recoverable=True),
            data={"resolved_exe": resolution.as_payload(), "args": arg_list, "cwd": workdir},
        )

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
            ),
            data=base_data,
        )

    attach_result = debug_session_start(
        process_id=launched_pid,
        cwd=workdir,
        env=env,
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
        try:
            os.kill(launched_pid, signal.SIGKILL)
            data["cleanup_action"] = "killed_launched_process"
        except OSError:
            data["cleanup_action"] = "kill_launched_process_failed"
        return _envelope(
            ok=False,
            session_id=session_id,
            state=attach_result.get("state"),
            summary="Failed to attach lldb to launched process",
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
            )

    return _envelope(
        ok=True,
        session_id=str(session_id),
        state=attach_result.get("state"),
        summary=f"Launched process {launched_pid}, delayed {attach_delay_ms}ms, attached lldb",
        data=data,
    )


def debug_session_wait_ready(session_id: str, *, timeout_ms: int = DEFAULT_STARTUP_TIMEOUT_MS) -> dict[str, Any]:
    # lldb launch/attach is synchronous in the worker: a started session is
    # always ready, so this just reports the current snapshot.
    result = _wrap_worker_call(
        "session_status", {"session_id": session_id}, "Session readiness", session_id=session_id
    )
    if result.get("ok"):
        result["data"]["ready"] = True
        result["summary"] = "Session is ready"
    return result


def debug_session_list() -> dict[str, Any]:
    return _wrap_worker_call("session_list", {}, "Listed debug sessions")


def debug_session_cleanup(*, session_id: str = "", force: bool = True) -> dict[str, Any]:
    return _wrap_worker_call(
        "session_cleanup",
        {"session_id": session_id, "force": force},
        "Cleaned debug sessions",
        session_id=session_id or None,
    )


def debug_session_status(session_id: str) -> dict[str, Any]:
    result = _wrap_worker_call(
        "session_status", {"session_id": session_id}, "Session status", session_id=session_id
    )
    if result.get("ok"):
        data = result["data"]
        data["current_thread"] = data.get("selected_thread_id")
        data["current_frame"] = data.get("selected_frame_index")
        data["last_stop_reason"] = (data.get("last_stop_event") or {}).get("reason")
        result["summary"] = f"Session is {data.get('state')}"
    return result


def debug_session_stop(
    session_id: str,
    *,
    kill_process: bool = True,
    detach: bool = False,
) -> dict[str, Any]:
    result = _wrap_worker_call(
        "session_stop",
        {"session_id": session_id, "kill_process": kill_process, "detach": detach},
        "Debug session stopped",
        session_id=session_id,
    )
    if result.get("ok"):
        result["data"]["detach"] = detach
        result["state"] = result["data"].get("final_state")
    return result


# ---------------------------------------------------------------------------
# Breakpoints / stepping / run control
# ---------------------------------------------------------------------------


def _parse_breakpoint_id(breakpoint_id: Any) -> int:
    try:
        return int(breakpoint_id)
    except (TypeError, ValueError):
        raise _CallError("invalid_input", f"Invalid breakpoint_id: {breakpoint_id!r}")


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
    if not ((source_file and line > 0) or symbol or address):
        return _envelope(
            ok=False,
            session_id=session_id,
            state=None,
            summary="Breakpoint location is required",
            error=_error(
                "invalid_input",
                "Provide source_file+line, symbol, or address",
                recoverable=True,
            ),
        )
    return _wrap_worker_call(
        "breakpoint_set",
        {
            "session_id": session_id,
            "source_file": source_file,
            "line": line,
            "symbol": symbol,
            "address": address,
            "condition": condition,
            "hit_count": hit_count,
            "thread_id": thread_id,
            "module": module,
        },
        "Breakpoint set",
        session_id=session_id,
    )


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
    return _envelope(
        ok=failures == 0,
        session_id=session_id,
        state=None,
        summary=f"Set {len(results) - failures}/{len(results)} breakpoints",
        data={"results": results, "failure_count": failures},
        error=None
        if failures == 0
        else _error(
            "breakpoint_unresolved",
            f"{failures} breakpoint(s) failed",
            recoverable=True,
            suggested_next_actions=["Inspect data.results[].error."],
        ),
    )


def debug_breakpoint_list(session_id: str) -> dict[str, Any]:
    return _wrap_worker_call(
        "breakpoint_list", {"session_id": session_id}, "Listed breakpoints", session_id=session_id
    )


def debug_breakpoint_remove(session_id: str, breakpoint_id: Any) -> dict[str, Any]:
    try:
        bp_id = _parse_breakpoint_id(breakpoint_id)
    except _CallError as exc:
        return _envelope(
            ok=False, session_id=session_id, state=None,
            summary="Invalid breakpoint_id",
            error=_error(exc.kind, exc.message, recoverable=True),
        )
    return _wrap_worker_call(
        "breakpoint_remove",
        {"session_id": session_id, "breakpoint_id": bp_id},
        "Removed breakpoint",
        session_id=session_id,
    )


def debug_breakpoint_enable(session_id: str, breakpoint_id: Any) -> dict[str, Any]:
    try:
        bp_id = _parse_breakpoint_id(breakpoint_id)
    except _CallError as exc:
        return _envelope(
            ok=False, session_id=session_id, state=None,
            summary="Invalid breakpoint_id",
            error=_error(exc.kind, exc.message, recoverable=True),
        )
    return _wrap_worker_call(
        "breakpoint_enable",
        {"session_id": session_id, "breakpoint_id": bp_id},
        "Enabled breakpoint",
        session_id=session_id,
    )


def debug_breakpoint_disable(session_id: str, breakpoint_id: Any) -> dict[str, Any]:
    try:
        bp_id = _parse_breakpoint_id(breakpoint_id)
    except _CallError as exc:
        return _envelope(
            ok=False, session_id=session_id, state=None,
            summary="Invalid breakpoint_id",
            error=_error(exc.kind, exc.message, recoverable=True),
        )
    return _wrap_worker_call(
        "breakpoint_disable",
        {"session_id": session_id, "breakpoint_id": bp_id},
        "Disabled breakpoint",
        session_id=session_id,
    )


def debug_run_until_stop(session_id: str, *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
    return _wrap_worker_call(
        "run_until_stop",
        {"session_id": session_id, "timeout_ms": timeout_ms},
        "Ran until stop",
        timeout_ms=timeout_ms + RUN_CONTROL_SLACK_MS,
        session_id=session_id,
    )


def debug_continue(session_id: str, *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
    return debug_run_until_stop(session_id, timeout_ms=timeout_ms)


def debug_step_over(session_id: str, *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
    return _wrap_worker_call(
        "step",
        {"session_id": session_id, "kind": "over", "timeout_ms": timeout_ms},
        "Stepped over",
        timeout_ms=timeout_ms + RUN_CONTROL_SLACK_MS,
        session_id=session_id,
    )


def debug_step_into(session_id: str, *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
    return _wrap_worker_call(
        "step",
        {"session_id": session_id, "kind": "into", "timeout_ms": timeout_ms},
        "Stepped into",
        timeout_ms=timeout_ms + RUN_CONTROL_SLACK_MS,
        session_id=session_id,
    )


def debug_step_out(session_id: str, *, timeout_ms: int = DEFAULT_TIMEOUT_MS) -> dict[str, Any]:
    return _wrap_worker_call(
        "step",
        {"session_id": session_id, "kind": "out", "timeout_ms": timeout_ms},
        "Stepped out",
        timeout_ms=timeout_ms + RUN_CONTROL_SLACK_MS,
        session_id=session_id,
    )


def debug_interrupt(session_id: str) -> dict[str, Any]:
    return _wrap_worker_call(
        "interrupt",
        {"session_id": session_id, "timeout_ms": DEFAULT_TIMEOUT_MS},
        "Interrupted debuggee",
        timeout_ms=DEFAULT_TIMEOUT_MS + RUN_CONTROL_SLACK_MS,
        session_id=session_id,
    )


# ---------------------------------------------------------------------------
# Inspection
# ---------------------------------------------------------------------------


def _first_project_frame(frames: list[dict[str, Any]]) -> dict[str, Any] | None:
    for frame in frames:
        if frame.get("is_project_frame"):
            return frame
    return frames[0] if frames else None


def debug_stack_get(session_id: str) -> dict[str, Any]:
    result = _wrap_worker_call(
        "stack_get", {"session_id": session_id}, "Captured stack", session_id=session_id
    )
    if result.get("ok"):
        frames = list(result["data"].get("frames") or [])
        result["data"]["first_project_frame"] = _first_project_frame(frames)
    return result


def debug_frame_select(session_id: str, frame: int) -> dict[str, Any]:
    return _wrap_worker_call(
        "frame_select",
        {"session_id": session_id, "frame": frame},
        "Selected frame",
        session_id=session_id,
    )


def debug_scopes_get(session_id: str, *, name_filter: str = "") -> dict[str, Any]:
    return _wrap_worker_call(
        "scopes_get",
        {"session_id": session_id, "name_filter": name_filter},
        "Captured locals",
        session_id=session_id,
    )


def debug_variable_expand(
    session_id: str,
    var_path: str,
    *,
    frame: int | None = None,
    max_children: int = 64,
    depth: int = 1,
) -> dict[str, Any]:
    if not var_path.strip():
        return _envelope(
            ok=False,
            session_id=session_id,
            state=None,
            summary="var_path is required",
            error=_error(
                "invalid_path",
                "var_path must be non-empty",
                recoverable=True,
                suggested_next_actions=list(_SUGGESTED_NEXT["invalid_path"]),
            ),
        )
    return _wrap_worker_call(
        "variable_expand",
        {
            "session_id": session_id,
            "var_path": var_path,
            "frame": frame,
            "max_children": max_children,
            "depth": depth,
        },
        "Expanded variable",
        session_id=session_id,
    )


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
    return _wrap_worker_call(
        "expression_eval",
        {
            "session_id": session_id,
            "expression": expression,
            "frame": frame,
            "thread_id": thread_id,
        },
        "Evaluated expression",
        session_id=session_id,
    )


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
    return _wrap_worker_call(
        "memory_read",
        {"session_id": session_id, "address": address, "size": size, "format": format},
        "Read memory",
        session_id=session_id,
    )


def debug_registers_get(session_id: str) -> dict[str, Any]:
    return _wrap_worker_call(
        "registers_get", {"session_id": session_id}, "Captured registers", session_id=session_id
    )


def debug_threads_get(session_id: str) -> dict[str, Any]:
    return _wrap_worker_call(
        "threads_get", {"session_id": session_id}, "Captured threads", session_id=session_id
    )


def debug_thread_select(session_id: str, thread_id: int) -> dict[str, Any]:
    return _wrap_worker_call(
        "thread_select",
        {"session_id": session_id, "thread_id": thread_id},
        "Selected thread",
        session_id=session_id,
    )


# ---------------------------------------------------------------------------
# Extended
# ---------------------------------------------------------------------------


def debug_modules_get(session_id: str, *, filter_text: str = "") -> dict[str, Any]:
    return _wrap_worker_call(
        "modules_get",
        {"session_id": session_id, "filter_text": filter_text},
        "Listed modules",
        session_id=session_id,
    )


def debug_crash_report(
    session_id: str,
    *,
    frames_per_thread: int = 32,
    log_tail_lines: int = 60,
) -> dict[str, Any]:
    return _wrap_worker_call(
        "crash_report",
        {
            "session_id": session_id,
            "frames_per_thread": frames_per_thread,
            "log_tail_lines": log_tail_lines,
        },
        "Captured crash report",
        session_id=session_id,
    )


def debug_pdb_resolve(
    *,
    symbol: str = "",
    source_file: str = "",
    line: int = 0,
    exe_target: str = "",
    exe_path: str = "",
    build_dir: str = "",
) -> dict[str, Any]:
    return _not_supported(
        "debug_pdb_resolve",
        "PDB is a Windows symbol format; on macOS DWARF debug info is embedded "
        "in the binary and lldb resolves file:line/symbol directly. "
        "Use debug_breakpoint_set with source_file+line or symbol.",
    )


def debug_crash_dump_analyze(
    dump_path: str,
    *,
    symbols: str = "",
    top_frames: int = 24,
    timeout_ms: int = 120_000,
) -> dict[str, Any]:
    return _not_supported(
        "debug_crash_dump_analyze",
        "Windows .dmp analysis is not supported by the macOS LLDB backend. "
        "Use debug_session_start for a live repro instead.",
    )


def debug_heap_stat(session_id: str) -> dict[str, Any]:
    return _not_supported(
        "debug_heap_stat",
        "Heap statistics (!heap) are cdb-specific and not implemented on the "
        "macOS LLDB backend.",
    )


def debug_watchpoint_set(
    session_id: str,
    *,
    address: str = "",
    expression: str = "",
    access: str = "write",
    size: int = 4,
) -> dict[str, Any]:
    if not address.strip() and not expression.strip():
        return _envelope(
            ok=False,
            session_id=session_id,
            state=None,
            summary="address or expression required",
            error=_error("invalid_input", "Provide address or expression", recoverable=True),
        )
    return _wrap_worker_call(
        "watchpoint_set",
        {
            "session_id": session_id,
            "address": address,
            "expression": expression,
            "access": access,
            "size": size,
        },
        "Watchpoint set",
        session_id=session_id,
    )


# ---------------------------------------------------------------------------
# Process discovery / control
# ---------------------------------------------------------------------------


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
                suggested_next_actions=["Pass a process name fragment such as EpicMapEditor."],
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
            error=_error("backend_error", str(exc), recoverable=True),
        )
    return _envelope(
        ok=True,
        session_id=None,
        state=None,
        summary=f"Found {len(matches)} matching processes" if matches else "No matching processes found",
        data={"query": query, "matches": matches, "match_count": len(matches)},
    )


def _pid_gone_or_zombie(pid: int) -> bool:
    """True when the pid is reaped or already a zombie (SIGKILL delivered)."""
    try:
        proc = subprocess.run(
            ["ps", "-p", str(pid), "-o", "stat="],
            capture_output=True,
            text=True,
            timeout=10,
            check=False,
        )
    except (OSError, subprocess.TimeoutExpired):
        return True
    stat = (proc.stdout or "").strip().upper()
    if not stat:
        return True
    return stat.startswith("Z")


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

    try:
        os.kill(process_id, signal.SIGKILL)
    except OSError as exc:
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Failed to terminate target process",
            error=_error("terminate_failed", str(exc), recoverable=True),
            data={"process": process, "terminated": False},
        )
    deadline = time.monotonic() + 3.0
    while not _pid_gone_or_zombie(process_id) and time.monotonic() < deadline:
        time.sleep(0.05)
    if not _pid_gone_or_zombie(process_id):
        return _envelope(
            ok=False,
            session_id=None,
            state=None,
            summary="Failed to terminate target process",
            error=_error(
                "terminate_failed",
                f"Unable to terminate process {process_id} ({process['process_name']})",
                recoverable=True,
            ),
            data={"process": process, "terminated": False},
        )
    return _envelope(
        ok=True,
        session_id=None,
        state=None,
        summary="Terminated target process",
        data={"process": process, "terminated": True},
    )


def reset_sessions_for_tests() -> None:
    try:
        _call("session_cleanup", {"force": True}, timeout_ms=DEFAULT_TIMEOUT_MS)
    except _CallError:
        pass
    _reset_worker()
