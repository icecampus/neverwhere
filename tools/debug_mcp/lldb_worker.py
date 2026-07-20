"""LLDB SB API worker for the neverwhere debug MCP server (macOS backend).

Runs under the system ``/usr/bin/python3`` (3.9) because Xcode ships the
``lldb`` Python module only for cp39 (see ``xcrun lldb -P``), while the MCP
SDK requires Python >= 3.10. The MCP server (``lldb_backend.py``) spawns
this worker and talks to it over a JSON-lines protocol on stdin/stdout:

    request:  {"id": <int>, "cmd": "<name>", "args": {...}}
    response: {"id": <int>, "ok": true, "result": {...}}
              {"id": <int>, "ok": false, "error": {"kind": "...", "message": "..."}}

One request at a time, exactly one response per request. Debug sessions
live in this process: session_id -> SBDebugger/SBTarget/SBProcess.

Python 3.9 compatibility is mandatory here: no match statements, no PEP 604
runtime unions (``int | str`` outside annotations), no 3.10+ stdlib.
"""

from __future__ import annotations

import atexit
import json
import os
import signal
import sys
import tempfile
import time
import traceback
import uuid
from typing import Any, Dict, List, Optional

try:
    import lldb
except ImportError:
    # Resolve Xcode's lldb python module when PYTHONPATH was not injected.
    import subprocess

    _lldb_python_dir = subprocess.check_output(["xcrun", "lldb", "-P"], text=True).strip()
    sys.path.insert(0, _lldb_python_dir)
    import lldb

PROTOCOL_VERSION = 1

# Contract session states (mirrors the cdb backend's SESSION_STATES).
STOPPED_STATES = (lldb.eStateStopped, lldb.eStateCrashed)
TERMINAL_STATES = (
    lldb.eStateExited,
    lldb.eStateDetached,
    lldb.eStateUnloaded,
)

_STOP_REASON_NAMES = {
    lldb.eStopReasonInvalid: "invalid",
    lldb.eStopReasonNone: "none",
    lldb.eStopReasonTrace: "trace",
    lldb.eStopReasonBreakpoint: "breakpoint",
    lldb.eStopReasonWatchpoint: "watchpoint",
    lldb.eStopReasonSignal: "signal",
    lldb.eStopReasonException: "exception",
    lldb.eStopReasonExec: "exec",
    lldb.eStopReasonPlanComplete: "plan-complete",
    lldb.eStopReasonThreadExiting: "thread-exiting",
    lldb.eStopReasonInstrumentation: "instrumentation",
}
if hasattr(lldb, "eStopReasonProcessorTrace"):
    _STOP_REASON_NAMES[lldb.eStopReasonProcessorTrace] = "processor-trace"

_STATE_NAMES = {
    lldb.eStateInvalid: "invalid",
    lldb.eStateUnloaded: "unloaded",
    lldb.eStateConnected: "connected",
    lldb.eStateAttaching: "attaching",
    lldb.eStateLaunching: "launching",
    lldb.eStateStopped: "stopped",
    lldb.eStateRunning: "running",
    lldb.eStateStepping: "stepping",
    lldb.eStateCrashed: "crashed",
    lldb.eStateDetached: "detached",
    lldb.eStateExited: "exited",
    lldb.eStateSuspended: "suspended",
}


class WorkerError(Exception):
    """Error with a contract ``kind`` that the backend wraps into an envelope."""

    def __init__(self, kind: str, message: str):
        super().__init__(message)
        self.kind = kind
        self.message = message


def _state_name(state: int) -> str:
    return _STATE_NAMES.get(state, "unknown(%d)" % state)


def _session_state(state: int) -> str:
    """Map an SBState to the contract session state."""
    if state in (lldb.eStateStopped, lldb.eStateCrashed, lldb.eStateSuspended):
        return "stopped"
    if state in (lldb.eStateRunning, lldb.eStateStepping):
        return "running"
    if state in (lldb.eStateLaunching, lldb.eStateAttaching, lldb.eStateConnected):
        return "launching"
    if state in (lldb.eStateExited, lldb.eStateDetached, lldb.eStateUnloaded):
        return "exited"
    return "error"


def _signal_name(signo: int) -> str:
    try:
        return signal.Signals(signo).name
    except (ValueError, AttributeError):
        return "signal %d" % signo


class LldbSession:
    """One debug session: its own SBDebugger/SBTarget/SBProcess."""

    def __init__(self, repo_root: str):
        self.session_id = uuid.uuid4().hex[:12]
        self.repo_root = os.path.realpath(repo_root) if repo_root else ""
        self.scratch_dir = tempfile.mkdtemp(prefix="nw_lldb_%s_" % self.session_id)
        self.debugger = lldb.SBDebugger.Create()
        self.debugger.SetAsync(True)
        # Keep LLDB's own chatter off stdout — the wire protocol lives there.
        devnull = open(os.devnull, "w")
        self._devnull = devnull
        self.debugger.SetOutputFileHandle(devnull, True)
        self.debugger.SetErrorFileHandle(devnull, True)
        self.target: Optional["lldb.SBTarget"] = None
        self.process: Optional["lldb.SBProcess"] = None
        # Process state events are drained through the debugger's listener —
        # in this lldb build the public SBProcess state only settles reliably
        # when someone consumes the event queue.
        self.listener = self.debugger.GetListener()
        self.attached = False
        self.exe = ""
        self.args: List[str] = []
        self.cwd = ""
        self.ready = False
        self.selected_thread_id: Optional[int] = None
        self.selected_frame_index = 0
        self.last_stop_event: Optional[Dict[str, Any]] = None
        self.created_at = time.time()
        self.last_activity_at = self.created_at
        self.diagnostics: List[str] = []

    # -- helpers -----------------------------------------------------------

    def touch(self) -> None:
        self.last_activity_at = time.time()

    def require_process(self) -> "lldb.SBProcess":
        process = self.process
        if process is None or not process.IsValid():
            raise WorkerError("invalid_state", "No live process in session %s" % self.session_id)
        return process

    def state(self) -> str:
        process = self.process
        if process is None or not process.IsValid():
            return "exited"
        return _session_state(process.GetState())

    def current_thread(self) -> "lldb.SBThread":
        process = self.require_process()
        if self.selected_thread_id is not None:
            thread = process.GetThreadByID(self.selected_thread_id)
            if thread is not None and thread.IsValid():
                return thread
        thread = _pick_stop_thread(process)
        if thread is None:
            raise WorkerError("invalid_state", "No threads in process")
        return thread

    def current_frame(self) -> "lldb.SBFrame":
        thread = self.current_thread()
        num_frames = thread.GetNumFrames()
        if num_frames == 0:
            raise WorkerError("invalid_state", "Current thread has no frames")
        index = min(max(self.selected_frame_index, 0), num_frames - 1)
        return thread.GetFrameAtIndex(index)

    def destroy(self, kill_process: bool, detach: bool) -> Dict[str, Any]:
        process = self.process
        final_state = self.state()
        killed = False
        detached = False
        if process is not None and process.IsValid():
            if detach:
                err = process.Detach()
                detached = bool(err.Success())
            elif kill_process:
                err = process.Kill()
                killed = bool(err.Success())
                final_state = "exited"
        self.process = None
        self.target = None
        if self.debugger is not None and self.debugger.IsValid():
            lldb.SBDebugger.Destroy(self.debugger)
        self.debugger = None
        try:
            self._devnull.close()
        except Exception:
            pass
        return {
            "final_state": "exited" if detached else final_state,
            "killed": killed,
            "detached": detached,
            "session_id": self.session_id,
        }


SESSIONS: Dict[str, LldbSession] = {}


def _get_session(args: Dict[str, Any]) -> LldbSession:
    session_id = str(args.get("session_id") or "")
    session = SESSIONS.get(session_id)
    if session is None:
        raise WorkerError("session_not_found", "Unknown session_id: %s" % session_id)
    session.touch()
    return session


def _pick_stop_thread(process: "lldb.SBProcess") -> Optional["lldb.SBThread"]:
    """First thread with a real stop reason; falls back to thread 0."""
    fallback = None
    for i in range(process.GetNumThreads()):
        thread = process.GetThreadAtIndex(i)
        if not thread.IsValid():
            continue
        if fallback is None:
            fallback = thread
        if thread.GetStopReason() not in (lldb.eStopReasonInvalid, lldb.eStopReasonNone):
            return thread
    return fallback


def _process_has_stop_reason(process: "lldb.SBProcess") -> bool:
    for i in range(process.GetNumThreads()):
        thread = process.GetThreadAtIndex(i)
        if thread.IsValid() and thread.GetStopReason() not in (
            lldb.eStopReasonInvalid,
            lldb.eStopReasonNone,
        ):
            return True
    return False


def _wait_for_settled(session: LldbSession, timeout_ms: int) -> int:
    """Wait until the process reaches a real stop (or terminal state).

    Events are drained through the debugger listener (required for the public
    SBProcess state to settle in this lldb build); ``GetState()`` is polled as
    a fallback. Transient ``stopped`` states with no stop reason (mid-launch,
    mid-resume) do not count as a stop.
    """
    process = session.require_process()
    listener = session.listener
    event = lldb.SBEvent()
    deadline = time.monotonic() + max(0, timeout_ms) / 1000.0
    while True:
        while listener is not None and listener.GetNextEvent(event):
            pass  # draining the queue drives public state updates
        state = process.GetState()
        if state in TERMINAL_STATES:
            return state
        if state in STOPPED_STATES and _process_has_stop_reason(process):
            return state
        if state == lldb.eStateSuspended:
            return state
        if time.monotonic() >= deadline:
            return state
        time.sleep(0.005)


def _file_path(file_spec: "lldb.SBFileSpec") -> str:
    if not file_spec.IsValid():
        return ""
    directory = file_spec.GetDirectory() or ""
    filename = file_spec.GetFilename() or ""
    if not filename:
        return directory
    if not directory:
        return filename
    return os.path.join(directory, filename)


def _frame_dict(session: LldbSession, frame: "lldb.SBFrame", index: int) -> Dict[str, Any]:
    line_entry = frame.GetLineEntry()
    path = _file_path(line_entry.GetFileSpec())
    module = frame.GetModule()
    module_name = ""
    if module.IsValid():
        module_name = module.GetFileSpec().GetFilename() or ""
    pc = frame.GetPCAddress().GetLoadAddress(session.target)
    is_project = False
    if path and session.repo_root:
        try:
            is_project = os.path.realpath(path).startswith(session.repo_root + os.sep)
        except Exception:
            is_project = path.startswith(session.repo_root)
    return {
        "index": index,
        "function": frame.GetFunctionName() or frame.GetSymbol().GetName() or "",
        "file": path,
        "line": line_entry.GetLine(),
        "column": line_entry.GetColumn(),
        "address": hex(pc) if pc != lldb.LLDB_INVALID_ADDRESS else None,
        "module": module_name,
        "is_project_frame": is_project,
    }


_MACH_EXCEPTIONS = {
    1: ("EXC_BAD_ACCESS", "SIGSEGV"),
    2: ("EXC_BAD_INSTRUCTION", "SIGILL"),
    3: ("EXC_ARITHMETIC", "SIGFPE"),
    4: ("EXC_EMULATION", None),
    5: ("EXC_SOFTWARE", None),
    6: ("EXC_BREAKPOINT", "SIGTRAP"),
    7: ("EXC_SYSCALL", None),
    8: ("EXC_MACH_SYSCALL", None),
    9: ("EXC_RPC_ALERT", None),
    10: ("EXC_CRASH", "SIGABRT"),
    11: ("EXC_RESOURCE", None),
    12: ("EXC_GUARD", None),
}


def _stop_thread_event(session: LldbSession, thread: "lldb.SBThread") -> Dict[str, Any]:
    reason = thread.GetStopReason()
    event: Dict[str, Any] = {
        "reason": _STOP_REASON_NAMES.get(reason, "unknown(%d)" % reason),
        "thread_id": thread.GetThreadID(),
        "thread_index": thread.GetIndexID(),
        "stop_description": thread.GetStopDescription(4096) or "",
    }
    if reason == lldb.eStopReasonSignal:
        signo = thread.GetStopReasonDataAtIndex(0)
        event["signal"] = {"number": signo, "name": _signal_name(signo)}
    elif reason == lldb.eStopReasonException:
        # Darwin reports hardware faults as Mach exceptions (EXC_BAD_ACCESS
        # for a segfault) rather than BSD signals.
        code = thread.GetStopReasonDataAtIndex(0)
        subcode = thread.GetStopReasonDataAtIndex(1)
        name, signal_equivalent = _MACH_EXCEPTIONS.get(code, ("exception %d" % code, None))
        event["exception"] = {
            "number": code,
            "name": name,
            "subcode": subcode,
            "signal_equivalent": signal_equivalent,
        }
    elif reason == lldb.eStopReasonBreakpoint:
        event["breakpoint_id"] = thread.GetStopReasonDataAtIndex(0)
        event["breakpoint_location_id"] = thread.GetStopReasonDataAtIndex(1)
    elif reason == lldb.eStopReasonWatchpoint:
        event["watchpoint_id"] = thread.GetStopReasonDataAtIndex(0)
    if thread.GetNumFrames() > 0:
        event["frame"] = _frame_dict(session, thread.GetFrameAtIndex(0), 0)
    return event


def _build_stop_event(session: LldbSession, state: int) -> Dict[str, Any]:
    process = session.require_process()
    event: Dict[str, Any] = {
        "state": _state_name(state),
        "session_state": _session_state(state),
        "reason": None,
    }
    if state == lldb.eStateExited:
        event["reason"] = "exited"
        event["exit_status"] = process.GetExitStatus()
        event["exit_description"] = process.GetExitDescription() or ""
        session.last_stop_event = event
        return event
    thread = _pick_stop_thread(process)
    if thread is None:
        event["reason"] = "none"
        session.last_stop_event = event
        return event
    session.selected_thread_id = thread.GetThreadID()
    session.selected_frame_index = 0
    event.update(_stop_thread_event(session, thread))
    session.last_stop_event = event
    return event


def _continue_and_wait(session: LldbSession, timeout_ms: int) -> Dict[str, Any]:
    process = session.require_process()
    state = process.GetState()
    if state in STOPPED_STATES:
        err = process.Continue()
        # A transient "stopped" snapshot can race with the real running state.
        if not err.Success() and "already running" not in str(err.GetCString()):
            raise WorkerError("backend_error", "Continue failed: %s" % err.GetCString())
    settled = _wait_for_settled(session, timeout_ms)
    if settled not in STOPPED_STATES and settled not in TERMINAL_STATES:
        raise WorkerError(
            "timeout",
            "Process still running after %d ms" % timeout_ms,
        )
    return _build_stop_event(session, settled)


def _value_summary(value: "lldb.SBValue") -> Optional[str]:
    summary = value.GetSummary()
    if summary:
        return summary
    try:
        description = value.GetObjectDescription()
    except Exception:
        description = None
    if description:
        description = description.strip()
        if len(description) > 200:
            description = description[:200] + "..."
        return description or None
    return None


def _variable_dict(value: "lldb.SBValue", expand_children: bool) -> Dict[str, Any]:
    item: Dict[str, Any] = {
        "name": value.GetName() or "",
        "type": value.GetTypeName() or "",
        "value": value.GetValue(),
        "summary": _value_summary(value),
        "num_children": value.GetNumChildren(),
        "in_scope": value.IsInScope(),
    }
    if expand_children:
        children = []
        for i in range(min(value.GetNumChildren(), 32)):
            child = value.GetChildAtIndex(i)
            if child.IsValid():
                children.append(
                    {
                        "name": child.GetName() or "",
                        "type": child.GetTypeName() or "",
                        "value": child.GetValue(),
                        "summary": _value_summary(child),
                    }
                )
        item["children"] = children
    return item


def _parse_address(session: LldbSession, address: str) -> int:
    text = str(address).strip()
    if not text:
        raise WorkerError("invalid_input", "address is required")
    try:
        return int(text, 0)
    except ValueError:
        pass
    frame = session.current_frame()
    value = frame.EvaluateExpression(text)
    err = value.GetError()
    if not err.Success():
        raise WorkerError(
            "invalid_input",
            "Cannot parse address '%s': %s" % (text, err.GetCString()),
        )
    numeric = value.GetValue()
    if numeric is None:
        raise WorkerError("invalid_input", "Expression '%s' has no address value" % text)
    try:
        return int(numeric, 0)
    except ValueError:
        raise WorkerError("invalid_input", "Expression '%s' is not an address" % text)


def _breakpoint_payload(session: LldbSession, bp: "lldb.SBBreakpoint") -> Dict[str, Any]:
    resolved = bp.GetNumResolvedLocations() > 0
    locations = []
    for i in range(min(bp.GetNumLocations(), 8)):
        loc = bp.GetLocationAtIndex(i)
        if not loc.IsValid():
            continue
        address = loc.GetAddress()
        locations.append(
            {
                "id": loc.GetID(),
                "resolved": loc.IsResolved(),
                "address": hex(address.GetLoadAddress(session.target)),
            }
        )
    return {
        "breakpoint_id": bp.GetID(),
        "enabled": bp.IsEnabled(),
        "resolved": resolved,
        "binding_status": "bound" if resolved else "pending_deferred",
        "hit_count": bp.GetHitCount(),
        "ignore_count": bp.GetIgnoreCount(),
        "condition": bp.GetCondition() or "",
        "num_locations": bp.GetNumLocations(),
        "num_resolved_locations": bp.GetNumResolvedLocations(),
        "locations": locations,
    }


def _resolved_location_string(session: LldbSession, bp: "lldb.SBBreakpoint") -> str:
    for i in range(bp.GetNumLocations()):
        loc = bp.GetLocationAtIndex(i)
        if not (loc.IsValid() and loc.IsResolved()):
            continue
        ctx = loc.GetAddress().GetSymbolContext(lldb.eSymbolContextEverything)
        path = _file_path(ctx.GetLineEntry().GetFileSpec())
        line = ctx.GetLineEntry().GetLine()
        if path:
            return "%s:%d" % (path, line)
        function = ctx.GetFunction()
        if function.IsValid():
            return function.GetName() or ""
        module = ctx.GetModule()
        if module.IsValid():
            return module.GetFileSpec().GetFilename() or ""
    return ""


# ---------------------------------------------------------------------------
# Commands
# ---------------------------------------------------------------------------


def cmd_ping(args: Dict[str, Any]) -> Dict[str, Any]:
    return {
        "worker": "lldb_worker",
        "protocol": PROTOCOL_VERSION,
        "python": sys.version.split()[0],
        "lldb_version": lldb.SBDebugger.GetVersionString(),
        "pid": os.getpid(),
        "session_count": len(SESSIONS),
    }


def cmd_self_check(args: Dict[str, Any]) -> Dict[str, Any]:
    debugger = lldb.SBDebugger.Create()
    valid = debugger.IsValid()
    if valid:
        lldb.SBDebugger.Destroy(debugger)
    return {
        "lldb_imported": True,
        "lldb_version": lldb.SBDebugger.GetVersionString(),
        "debugger_create_ok": valid,
        "python": sys.version.split()[0],
    }


def cmd_session_start(args: Dict[str, Any]) -> Dict[str, Any]:
    exe = str(args.get("exe") or "").strip()
    process_id = int(args.get("process_id") or 0)
    break_on_start = bool(args.get("break_on_start", True))
    startup_timeout_ms = int(args.get("startup_timeout_ms") or 30000)
    repo_root = str(args.get("repo_root") or "")
    if not exe and process_id <= 0:
        raise WorkerError("invalid_input", "exe or process_id is required")
    if exe and not os.path.isfile(exe):
        raise WorkerError("launch_failed", "Executable not found: %s" % exe)

    session = LldbSession(repo_root)
    session.exe = exe
    session.args = [str(a) for a in (args.get("args") or [])]
    session.cwd = str(args.get("cwd") or "") or os.getcwd()

    target = session.debugger.CreateTarget(exe) if exe else session.debugger.CreateTarget(None)
    if target is None or not target.IsValid():
        raise WorkerError("launch_failed", "Failed to create target for %s" % (exe or "<attach>"))
    session.target = target

    if process_id > 0:
        attach_info = lldb.SBAttachInfo(process_id)
        attach_info.SetWaitForLaunch(False)
        err = lldb.SBError()
        process = target.Attach(attach_info, err)
        if process is None or not process.IsValid() or not err.Success():
            message = err.GetCString() if err is not None else "attach failed"
            raise WorkerError(
                "launch_failed",
                "Attach to pid %d failed: %s" % (process_id, message),
            )
        session.attached = True
        session.process = process
        state = _wait_for_settled(session, startup_timeout_ms)
        if state not in STOPPED_STATES + TERMINAL_STATES:
            session.diagnostics.append(
                "attach did not reach a stopped state within %d ms" % startup_timeout_ms
            )
        event = _build_stop_event(session, process.GetState())
        if not break_on_start and process.GetState() in STOPPED_STATES:
            process.Continue()
            event = {"state": "running", "session_state": "running", "reason": None}
    else:
        argv = [exe] + session.args
        launch_info = lldb.SBLaunchInfo(argv)
        launch_info.SetWorkingDirectory(session.cwd)
        merged_env = dict(os.environ)
        for key, value in (args.get("env") or {}).items():
            merged_env[str(key)] = str(value)
        launch_info.SetEnvironmentEntries(
            ["%s=%s" % (k, v) for k, v in merged_env.items()], False
        )
        if break_on_start:
            launch_info.SetLaunchFlags(lldb.eLaunchFlagStopAtEntry)
        stdout_path = os.path.join(session.scratch_dir, "inferior.stdout.log")
        stderr_path = os.path.join(session.scratch_dir, "inferior.stderr.log")
        launch_info.AddOpenFileAction(1, stdout_path, False, True)
        launch_info.AddOpenFileAction(2, stderr_path, False, True)
        err = lldb.SBError()
        process = target.Launch(launch_info, err)
        if process is None or not process.IsValid() or not err.Success():
            message = err.GetCString() if err is not None else "launch failed"
            raise WorkerError("launch_failed", "Launch of %s failed: %s" % (exe, message))
        session.process = process
        if break_on_start:
            state = _wait_for_settled(session, startup_timeout_ms)
            if state not in STOPPED_STATES and state not in TERMINAL_STATES:
                session.diagnostics.append(
                    "launch did not reach the entry stop within %d ms" % startup_timeout_ms
                )
            event = _build_stop_event(session, state)
            if event.get("reason") in ("signal", "exec"):
                # Stop-at-entry arrives as SIGSTOP/exec — present it as initial stop.
                event["reason"] = "initial_stop"
                session.last_stop_event = event
        else:
            # Return promptly without settling: the inferior is still before
            # main, so breakpoints set right after launch bind deterministically.
            state = process.GetState()
            event = {
                "state": _state_name(state),
                "session_state": _session_state(state),
                "reason": None,
            }
            session.last_stop_event = event

    session.ready = True
    SESSIONS[session.session_id] = session
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "ready": session.ready,
        "attached": session.attached,
        "process_id": process.GetProcessID(),
        "exe": exe,
        "args": session.args,
        "cwd": session.cwd,
        "break_on_start": break_on_start,
        "stdout_path": (
            os.path.join(session.scratch_dir, "inferior.stdout.log") if not session.attached else None
        ),
        "stderr_path": (
            os.path.join(session.scratch_dir, "inferior.stderr.log") if not session.attached else None
        ),
        "last_stop_event": session.last_stop_event,
        "diagnostics": list(session.diagnostics),
    }


def _session_snapshot(session: LldbSession) -> Dict[str, Any]:
    process = session.process
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "ready": session.ready,
        "attached": session.attached,
        "exe": session.exe,
        "process_id": process.GetProcessID() if process is not None and process.IsValid() else None,
        "selected_thread_id": session.selected_thread_id,
        "selected_frame_index": session.selected_frame_index,
        "last_stop_event": session.last_stop_event,
        "created_at": session.created_at,
        "last_activity_at": session.last_activity_at,
    }


def cmd_session_list(args: Dict[str, Any]) -> Dict[str, Any]:
    sessions = [_session_snapshot(s) for s in SESSIONS.values()]
    return {"sessions": sessions, "session_count": len(sessions)}


def cmd_session_status(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    snapshot = _session_snapshot(session)
    snapshot["diagnostics"] = list(session.diagnostics)
    return snapshot


def cmd_session_stop(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    kill_process = bool(args.get("kill_process", True))
    detach = bool(args.get("detach", False))
    result = session.destroy(kill_process=kill_process, detach=detach)
    SESSIONS.pop(session.session_id, None)
    result["state"] = result["final_state"]
    return result


def cmd_session_cleanup(args: Dict[str, Any]) -> Dict[str, Any]:
    force = bool(args.get("force", True))
    session_id = str(args.get("session_id") or "")
    if session_id:
        session = SESSIONS.get(session_id)
        if session is None:
            raise WorkerError("session_not_found", "Unknown session_id: %s" % session_id)
        targets = [session]
    else:
        targets = list(SESSIONS.values())
    cleaned = []
    for session in targets:
        cleaned.append(session.destroy(kill_process=force, detach=False))
        SESSIONS.pop(session.session_id, None)
    return {"cleaned": cleaned, "force": force}


def cmd_breakpoint_set(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    target = session.target
    if target is None or not target.IsValid():
        raise WorkerError("invalid_state", "Session has no target")
    source_file = str(args.get("source_file") or "").strip()
    line = int(args.get("line") or 0)
    symbol = str(args.get("symbol") or "").strip()
    address = str(args.get("address") or "").strip()
    condition = str(args.get("condition") or "").strip()
    hit_count = int(args.get("hit_count") or 0)
    thread_id = args.get("thread_id")
    module = str(args.get("module") or "").strip()

    if source_file and line > 0:
        file_spec = lldb.SBFileSpec(source_file, True)
        bp = target.BreakpointCreateByLocation(file_spec, line)
    elif symbol:
        bp = target.BreakpointCreateByName(symbol, module if module else None)
    elif address:
        bp = target.BreakpointCreateByAddress(_parse_address(session, address))
    else:
        raise WorkerError("invalid_input", "Provide source_file+line, symbol, or address")

    if bp is None or not bp.IsValid():
        raise WorkerError("breakpoint_unresolved", "Failed to create breakpoint")
    if condition:
        bp.SetCondition(condition)
    if hit_count > 0:
        # Contract: hit_count = break on the Nth hit.
        bp.SetIgnoreCount(max(0, hit_count - 1))
    if thread_id is not None:
        bp.SetThreadID(int(thread_id))

    payload = _breakpoint_payload(session, bp)
    payload["resolved_location"] = _resolved_location_string(session, bp)
    payload["session_id"] = session.session_id
    payload["state"] = session.state()
    payload["request"] = {
        "source_file": source_file or None,
        "line": line or None,
        "symbol": symbol or None,
        "address": address or None,
        "module": module or None,
    }
    return payload


def cmd_breakpoint_list(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    target = session.target
    breakpoints = []
    for i in range(target.GetNumBreakpoints()):
        bp = target.GetBreakpointAtIndex(i)
        if bp.IsValid():
            breakpoints.append(_breakpoint_payload(session, bp))
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "breakpoints": breakpoints,
        "count": len(breakpoints),
    }


def _find_breakpoint(session: LldbSession, breakpoint_id: int) -> "lldb.SBBreakpoint":
    bp = session.target.FindBreakpointByID(breakpoint_id)
    if bp is None or not bp.IsValid():
        raise WorkerError("invalid_input", "Unknown breakpoint_id: %s" % breakpoint_id)
    return bp


def cmd_breakpoint_remove(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    breakpoint_id = int(args.get("breakpoint_id"))
    _find_breakpoint(session, breakpoint_id)
    removed = session.target.BreakpointDelete(breakpoint_id)
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "removed": breakpoint_id,
        "success": bool(removed),
    }


def cmd_breakpoint_enable(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    breakpoint_id = int(args.get("breakpoint_id"))
    bp = _find_breakpoint(session, breakpoint_id)
    bp.SetEnabled(True)
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "breakpoint_id": breakpoint_id,
        "enabled": True,
    }


def cmd_breakpoint_disable(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    breakpoint_id = int(args.get("breakpoint_id"))
    bp = _find_breakpoint(session, breakpoint_id)
    bp.SetEnabled(False)
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "breakpoint_id": breakpoint_id,
        "enabled": False,
    }


def cmd_run_until_stop(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    timeout_ms = int(args.get("timeout_ms") or 30000)
    process = session.require_process()
    state = process.GetState()
    if state in TERMINAL_STATES:
        event = _build_stop_event(session, state)
    else:
        event = _continue_and_wait(session, timeout_ms)
    event["session_id"] = session.session_id
    return {"session_id": session.session_id, "state": session.state(), "stop_event": event}


def cmd_step(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    kind = str(args.get("kind") or "over")
    timeout_ms = int(args.get("timeout_ms") or 30000)
    process = session.require_process()
    if process.GetState() not in STOPPED_STATES:
        raise WorkerError("invalid_state", "Process is not stopped (state=%s)" % _state_name(process.GetState()))
    thread = session.current_thread()
    err = lldb.SBError()
    if kind == "over":
        thread.StepOver(err)
    elif kind == "into":
        thread.StepInto(err)
    elif kind == "out":
        thread.StepOut(err)
    else:
        raise WorkerError("invalid_input", "Unknown step kind: %s" % kind)
    if not err.Success():
        raise WorkerError("backend_error", "Step failed: %s" % err.GetCString())
    state = _wait_for_settled(session, timeout_ms)
    if state not in STOPPED_STATES and state not in TERMINAL_STATES:
        raise WorkerError("timeout", "Step did not complete within %d ms" % timeout_ms)
    event = _build_stop_event(session, state)
    if event.get("reason") in ("plan-complete", "trace", "none"):
        event["reason"] = "step_complete"
        session.last_stop_event = event
    event["session_id"] = session.session_id
    return {"session_id": session.session_id, "state": session.state(), "stop_event": event}


def cmd_interrupt(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    process = session.require_process()
    timeout_ms = int(args.get("timeout_ms") or 10000)
    err = process.SendAsyncInterrupt()
    if not err.Success():
        raise WorkerError("backend_error", "Interrupt failed: %s" % err.GetCString())
    state = _wait_for_settled(session, timeout_ms)
    if state not in STOPPED_STATES and state not in TERMINAL_STATES:
        raise WorkerError("timeout", "Interrupt did not stop the process within %d ms" % timeout_ms)
    event = _build_stop_event(session, state)
    event["reason"] = "manual_interrupt"
    session.last_stop_event = event
    event["session_id"] = session.session_id
    return {"session_id": session.session_id, "state": session.state(), "stop_event": event}


def cmd_stack_get(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    thread = session.current_thread()
    frames = []
    for i in range(thread.GetNumFrames()):
        frames.append(_frame_dict(session, thread.GetFrameAtIndex(i), i))
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "thread_id": thread.GetThreadID(),
        "frames": frames,
        "frame_count": len(frames),
    }


def cmd_frame_select(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    index = int(args.get("frame") or 0)
    thread = session.current_thread()
    if index < 0 or index >= thread.GetNumFrames():
        raise WorkerError(
            "invalid_input",
            "Frame index %d out of range (0..%d)" % (index, thread.GetNumFrames() - 1),
        )
    session.selected_frame_index = index
    frame = thread.GetFrameAtIndex(index)
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "current_frame": index,
        "frame": _frame_dict(session, frame, index),
    }


def cmd_scopes_get(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    name_filter = str(args.get("name_filter") or "").strip().casefold()
    frame = session.current_frame()

    def collect(values: "lldb.SBValueList") -> List[Dict[str, Any]]:
        items = []
        for i in range(values.GetSize()):
            value = values.GetValueAtIndex(i)
            if not value.IsValid():
                continue
            name = value.GetName() or ""
            expand = bool(name_filter) and name_filter in name.casefold()
            if name_filter and not expand:
                continue
            items.append(_variable_dict(value, expand))
        return items

    arg_values = frame.GetVariables(True, False, False, True)
    local_values = frame.GetVariables(False, True, True, True)
    args_items = collect(arg_values)
    locals_items = collect(local_values)
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "frame": session.selected_frame_index,
        "args": args_items,
        "locals": locals_items,
        "filter_applied": bool(name_filter),
        "name_filter": str(args.get("name_filter") or ""),
        "matched_count": len(args_items) + len(locals_items),
    }


def cmd_expression_eval(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    expression = str(args.get("expression") or "").strip()
    if not expression:
        raise WorkerError("invalid_expression", "expression must be non-empty")
    frame_index = args.get("frame")
    thread_id = args.get("thread_id")
    if thread_id is not None:
        session.selected_thread_id = int(thread_id)
    if frame_index is not None:
        session.selected_frame_index = int(frame_index)
    frame = session.current_frame()
    value = frame.EvaluateExpression(expression)
    err = value.GetError()
    if not err.Success():
        raise WorkerError("invalid_expression", err.GetCString() or "evaluation failed")
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "expression": expression,
        "value": value.GetValue(),
        "summary": _value_summary(value),
        "type": value.GetTypeName() or "",
        "evaluation_mode": "lldb_expr",
    }


def cmd_memory_read(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    address = _parse_address(session, str(args.get("address") or ""))
    size = int(args.get("size") or 0)
    size = max(1, min(size, 4096))
    fmt = str(args.get("format") or "bytes")
    process = session.require_process()
    err = lldb.SBError()
    data = process.ReadMemory(address, size, err)
    if not err.Success():
        raise WorkerError("backend_error", "ReadMemory failed: %s" % err.GetCString())
    if data is None:
        data = b""
    result: Dict[str, Any] = {
        "session_id": session.session_id,
        "state": session.state(),
        "address": hex(address),
        "size": len(data),
        "format": fmt,
        "bytes_hex": bytes(data).hex(),
    }
    if fmt == "dwords":
        result["values"] = [
            int.from_bytes(bytes(data[i : i + 4]), "little") for i in range(0, len(data) - 3, 4)
        ]
    elif fmt == "qwords":
        result["values"] = [
            int.from_bytes(bytes(data[i : i + 8]), "little") for i in range(0, len(data) - 7, 8)
        ]
    return result


def cmd_registers_get(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    frame = session.current_frame()
    registers = []
    register_sets = frame.GetRegisters()
    for i in range(register_sets.GetSize()):
        register_set = register_sets.GetValueAtIndex(i)
        set_name = register_set.GetName() or ""
        for j in range(register_set.GetNumChildren()):
            reg = register_set.GetChildAtIndex(j)
            if reg.IsValid():
                registers.append(
                    {"set": set_name, "name": reg.GetName() or "", "value": reg.GetValue()}
                )
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "registers": registers,
        "count": len(registers),
    }


def cmd_threads_get(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    process = session.require_process()
    threads = []
    for i in range(process.GetNumThreads()):
        thread = process.GetThreadAtIndex(i)
        if not thread.IsValid():
            continue
        threads.append(
            {
                "index_id": thread.GetIndexID(),
                "thread_id": thread.GetThreadID(),
                "name": thread.GetName() or "",
                "queue_name": thread.GetQueueName() or "",
                "stop_reason": _STOP_REASON_NAMES.get(thread.GetStopReason(), "unknown"),
                "num_frames": thread.GetNumFrames(),
                "is_selected": thread.GetThreadID() == session.selected_thread_id,
            }
        )
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "threads": threads,
        "count": len(threads),
    }


def cmd_thread_select(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    thread_id = int(args.get("thread_id"))
    process = session.require_process()
    thread = process.GetThreadByID(thread_id)
    if thread is None or not thread.IsValid():
        raise WorkerError("invalid_input", "Unknown thread_id: %s" % thread_id)
    session.selected_thread_id = thread_id
    session.selected_frame_index = 0
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "current_thread": thread_id,
    }


def cmd_modules_get(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    filter_text = str(args.get("filter_text") or "").strip().casefold()
    target = session.target
    modules = []
    for i in range(target.GetNumModules()):
        module = target.GetModuleAtIndex(i)
        if not module.IsValid():
            continue
        path = _file_path(module.GetFileSpec())
        name = module.GetFileSpec().GetFilename() or ""
        if filter_text and filter_text not in name.casefold() and filter_text not in path.casefold():
            continue
        header = module.GetObjectFileHeaderAddress()
        modules.append(
            {
                "name": name,
                "path": path,
                "uuid": module.GetUUIDString() or "",
                "load_address": hex(header.GetLoadAddress(target)),
            }
        )
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "modules": modules,
        "count": len(modules),
        "filter_text": str(args.get("filter_text") or ""),
    }


def cmd_watchpoint_set(args: Dict[str, Any]) -> Dict[str, Any]:
    session = _get_session(args)
    target = session.target
    address_text = str(args.get("address") or "").strip()
    expression = str(args.get("expression") or "").strip()
    access = str(args.get("access") or "write").lower()
    size = int(args.get("size") or 4)
    size = {1: 1, 2: 2, 4: 4, 8: 8}.get(size, 4)
    if not address_text and expression:
        frame = session.current_frame()
        value = frame.EvaluateExpression(expression)
        err = value.GetError()
        if not err.Success():
            raise WorkerError("invalid_expression", err.GetCString() or "evaluation failed")
        address_text = str(value.GetValue() or "")
    if not address_text:
        raise WorkerError("invalid_input", "Provide address or expression")
    address = _parse_address(session, address_text)
    if access == "execute":
        raise WorkerError("invalid_input", "execute watchpoints are not supported by WatchAddress")
    read_access = access in ("read", "readwrite")
    write_access = access in ("write", "readwrite")
    err = lldb.SBError()
    watchpoint = target.WatchAddress(address, size, read_access, write_access, err)
    if not err.Success() or watchpoint is None or not watchpoint.IsValid():
        raise WorkerError(
            "backend_error",
            "WatchAddress failed: %s" % (err.GetCString() if err is not None else "unknown"),
        )
    return {
        "session_id": session.session_id,
        "state": session.state(),
        "watchpoint_id": watchpoint.GetID(),
        "address": hex(address),
        "access": access,
        "size": size,
    }


COMMANDS = {
    "ping": cmd_ping,
    "self_check": cmd_self_check,
    "session_start": cmd_session_start,
    "session_list": cmd_session_list,
    "session_status": cmd_session_status,
    "session_stop": cmd_session_stop,
    "session_cleanup": cmd_session_cleanup,
    "breakpoint_set": cmd_breakpoint_set,
    "breakpoint_list": cmd_breakpoint_list,
    "breakpoint_remove": cmd_breakpoint_remove,
    "breakpoint_enable": cmd_breakpoint_enable,
    "breakpoint_disable": cmd_breakpoint_disable,
    "run_until_stop": cmd_run_until_stop,
    "continue": cmd_run_until_stop,
    "step": cmd_step,
    "interrupt": cmd_interrupt,
    "stack_get": cmd_stack_get,
    "frame_select": cmd_frame_select,
    "scopes_get": cmd_scopes_get,
    "expression_eval": cmd_expression_eval,
    "memory_read": cmd_memory_read,
    "registers_get": cmd_registers_get,
    "threads_get": cmd_threads_get,
    "thread_select": cmd_thread_select,
    "modules_get": cmd_modules_get,
    "watchpoint_set": cmd_watchpoint_set,
}


def _cleanup_all() -> None:
    for session in list(SESSIONS.values()):
        try:
            session.destroy(kill_process=not session.attached, detach=session.attached)
        except Exception:
            pass
    SESSIONS.clear()


def _write_response(payload: Dict[str, Any]) -> None:
    sys.stdout.write(json.dumps(payload, ensure_ascii=False) + "\n")
    sys.stdout.flush()


def main() -> int:
    atexit.register(_cleanup_all)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        request_id = None
        try:
            request = json.loads(line)
            request_id = request.get("id")
            cmd = str(request.get("cmd") or "")
            args = request.get("args") or {}
            handler = COMMANDS.get(cmd)
            if handler is None:
                raise WorkerError("invalid_input", "Unknown cmd: %s" % cmd)
            result = handler(args)
            _write_response({"id": request_id, "ok": True, "result": result})
        except WorkerError as exc:
            _write_response(
                {
                    "id": request_id,
                    "ok": False,
                    "error": {"kind": exc.kind, "message": exc.message},
                }
            )
        except Exception as exc:  # keep the worker alive on unexpected errors
            traceback.print_exc(file=sys.stderr)
            _write_response(
                {
                    "id": request_id,
                    "ok": False,
                    "error": {"kind": "backend_error", "message": str(exc)},
                }
            )
    return 0


if __name__ == "__main__":
    sys.exit(main())
