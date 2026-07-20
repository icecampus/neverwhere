"""neverwhere debug MCP server — debugger tools exposed over MCP.

Stdio transport. Started by ``tools/run_mcp_server.ps1`` (Windows) or
``tools/run_mcp_server.sh`` (macOS) via ``python -m tools.debug_mcp.server``.
Wraps the pure functions of the platform backend as ``@mcp.tool()`` entries:

- Windows: ``debugger_mcp.py`` (cdb backend)
- macOS: ``lldb_backend.py`` (LLDB SB API worker backend)

This is the neverwhere counterpart of sandbox's sandbox-dev MCP — without
any DESC bits. Live-debug C++ targets (EpicMapEditor, playgrounds) under
cdb/lldb; analyze crash dumps written by MiniDumpWriteDump (Windows only).
"""

from __future__ import annotations

import sys

from mcp.server.fastmcp import FastMCP

# Import the backend as a module, not by name: the @mcp.tool() wrappers
# below reuse the backend function names, and same-named imports would be
# shadowed by the wrappers, making every tool recurse into itself.
if sys.platform == "darwin":
    from tools.debug_mcp import lldb_backend as _dbg
else:
    from tools.debug_mcp import debugger_mcp as _dbg
from tools.debug_mcp.exe_resolver import (
    debug_overrides_payload,
    write_active_debug_config,
)
from tools.debug_mcp.repo_root import repo_root

_ON_MACOS = sys.platform == "darwin"

_INSTRUCTIONS_WINDOWS = """neverwhere debug MCP — cdb-backed Windows native debugger.

Workflow for a crash / assert / wrong runtime value:

1. ``debug_self_check`` — confirm cdb is found and the default exe exists.
2. ``debug_session_start`` (exe_target='EpicMapEditor') or ``debug_process_find``
   + ``debug_session_start`` (process_name='EpicMapEditor') to attach.
3. ``debug_run_until_stop`` to let the inferior run.
4. ``debug_stack_get`` / ``debug_scopes_get`` / ``debug_expression_eval`` to
   inspect the fault. The first *project* frame (under the repo root) is the
   real crash site — stack tops are usually CRT/SEH trampolines.
5. ``debug_session_stop`` to clean up.

For post-mortem analysis of a saved ``.dmp`` (once neverwhere writes
minidumps on SEH): ``debug_crash_dump_analyze``.

Hints:
- exe_target resolution: ``_intermediate_64/<config>/<target>.exe``.
- cdb search order: CRASH_ANALYSIS_DEBUGGER_PATH env, then
  ``C:\\Program Files (x86)\\Windows Kits\\10\\Debuggers\\x64\\cdb.exe``.
- Prefer MCP tools over hand-running cdb in a terminal — the backend keeps
  long-lived cdb sessions with marker-delimited command/response framing.
"""

_INSTRUCTIONS_MACOS = """neverwhere debug MCP — LLDB-backed macOS native debugger (SB API worker).

Workflow for a crash / assert / wrong runtime value:

1. ``debug_self_check`` — confirm lldb + the lldb worker + the default exe.
2. ``debug_session_start`` (exe_target='EpicMapEditor') to launch, or
   ``debug_process_find`` + ``debug_session_start`` (process_id=...) to attach.
3. ``debug_run_until_stop`` to let the inferior run (SIGSEGV surfaces as a
   Mach ``exception`` stop — EXC_BAD_ACCESS — with signal_equivalent SIGSEGV).
4. ``debug_stack_get`` / ``debug_scopes_get`` / ``debug_expression_eval`` to
   inspect the fault. The first *project* frame (is_project_frame=true, under
   the repo root) is the real crash site.
5. ``debug_session_stop`` to clean up.

Hints:
- exe_target resolution: ``_intermediate_64/src/apps/<target>/<config>/<target>``
  (also tries ``src/refs`` and ``src/tests`` layouts).
- The backend spawns one ``lldb_worker.py`` process under /usr/bin/python3
  (Xcode's lldb SB module is cp39-only); the worker is restarted on failure.
- ``debug_pdb_resolve`` / ``debug_crash_dump_analyze`` / ``debug_heap_stat``
  are Windows-only and return not_supported here.
"""

_INSTRUCTIONS = _INSTRUCTIONS_MACOS if _ON_MACOS else _INSTRUCTIONS_WINDOWS

mcp = FastMCP("neverwhere-debug", instructions=_INSTRUCTIONS)


# ---------------------------------------------------------------------------
# Environment / contract
# ---------------------------------------------------------------------------

@mcp.tool()
def debug_self_check() -> dict:
    """Verify cdb + default exe + PDB + llvm-pdbutil availability. Call first."""
    return _dbg.debug_self_check()


@mcp.tool()
def debug_resolve_cdb(custom_path: str = "") -> dict:
    """Locate cdb.exe (CRASH_ANALYSIS_DEBUGGER_PATH, Windows Kits, vcpkg)."""
    return _dbg.debug_resolve_cdb(custom_path)


@mcp.tool()
def debug_contract_get() -> dict:
    """Return the debugger MCP contract (session states, error kinds, etc.)."""
    return _dbg.debug_contract_get()


@mcp.tool()
def debug_overrides() -> dict:
    """Show how the debug exe target is resolved (env / active file / default)."""
    return debug_overrides_payload(repo_root())


@mcp.tool()
def debug_set_active(
    cmake_target: str = "",
    exe_path: str = "",
    config: str = "Debug",
    clear: bool = False,
) -> dict:
    """Pin the debug exe target by writing .zcode/debug_active.json."""
    return write_active_debug_config(
        repo_root(),
        cmake_target=cmake_target,
        exe_path=exe_path,
        config=config,
        clear=clear,
    )


# ---------------------------------------------------------------------------
# Sessions (launch / attach / lifecycle)
# ---------------------------------------------------------------------------

@mcp.tool()
def debug_session_start(
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
    startup_timeout_ms: int = 30000,
    reload_symbols: bool = True,
) -> dict:
    """Start a cdb session: launch an inferior, or attach to a running process."""
    return _dbg.debug_session_start(
        exe=exe,
        exe_target=exe_target,
        config=config,
        process_id=process_id,
        process_name=process_name,
        args=args,
        cwd=cwd,
        env=env,
        symbols=symbols,
        break_on_start=break_on_start,
        wait_ready=wait_ready,
        startup_timeout_ms=startup_timeout_ms,
        reload_symbols=reload_symbols,
    )


@mcp.tool()
def debug_launch_then_attach(
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
    startup_timeout_ms: int = 30000,
    reload_symbols: bool = False,
) -> dict:
    """Launch the inferior detached, then attach cdb after attach_delay_ms."""
    return _dbg.debug_launch_then_attach(
        exe=exe,
        exe_target=exe_target,
        config=config,
        args=args,
        cwd=cwd,
        env=env,
        attach_delay_ms=attach_delay_ms,
        breakpoints=breakpoints,
        break_on_attach=break_on_attach,
        wait_ready=wait_ready,
        startup_timeout_ms=startup_timeout_ms,
        reload_symbols=reload_symbols,
    )


@mcp.tool()
def debug_session_wait_ready(session_id: str, timeout_ms: int = 30000) -> dict:
    """Block until a freshly started session emits its READY marker."""
    return _dbg.debug_session_wait_ready(session_id, timeout_ms=timeout_ms)


@mcp.tool()
def debug_session_list() -> dict:
    """List all live cdb sessions."""
    return _dbg.debug_session_list()


@mcp.tool()
def debug_session_cleanup(session_id: str = "", force: bool = True) -> dict:
    """Reap stale sessions (or force-clean a specific one)."""
    return _dbg.debug_session_cleanup(session_id=session_id, force=force)


@mcp.tool()
def debug_session_status(session_id: str) -> dict:
    """Snapshot of one session: state, inferior pid, last stop event, op log."""
    return _dbg.debug_session_status(session_id)


@mcp.tool()
def debug_session_stop(session_id: str, kill_process: bool = True, detach: bool = False) -> dict:
    """Stop a cdb session (kill inferior by default; detach keeps it running)."""
    return _dbg.debug_session_stop(session_id, kill_process=kill_process, detach=detach)


# ---------------------------------------------------------------------------
# Breakpoints / stepping / run control
# ---------------------------------------------------------------------------

@mcp.tool()
def debug_breakpoint_set(
    session_id: str,
    source_file: str = "",
    line: int = 0,
    symbol: str = "",
    address: str = "",
    condition: str = "",
    hit_count: int = 0,
    thread_id: int | None = None,
    module: str = "",
) -> dict:
    """Set a breakpoint (source file:line, symbol, or address)."""
    return _dbg.debug_breakpoint_set(
        session_id,
        source_file=source_file,
        line=line,
        symbol=symbol,
        address=address,
        condition=condition,
        hit_count=hit_count,
        thread_id=thread_id,
        module=module,
    )


@mcp.tool()
def debug_breakpoint_set_bulk(session_id: str, breakpoints: list[dict]) -> dict:
    """Set many breakpoints in one shot."""
    return _dbg.debug_breakpoint_set_bulk(session_id, breakpoints)


@mcp.tool()
def debug_breakpoint_list(session_id: str) -> dict:
    """List all breakpoints in the session."""
    return _dbg.debug_breakpoint_list(session_id)


@mcp.tool()
def debug_breakpoint_remove(session_id: str, breakpoint_id: str) -> dict:
    """Remove a breakpoint by id."""
    return _dbg.debug_breakpoint_remove(session_id, breakpoint_id)


@mcp.tool()
def debug_breakpoint_enable(session_id: str, breakpoint_id: str) -> dict:
    """Enable a disabled breakpoint."""
    return _dbg.debug_breakpoint_enable(session_id, breakpoint_id)


@mcp.tool()
def debug_breakpoint_disable(session_id: str, breakpoint_id: str) -> dict:
    """Disable a breakpoint without removing it."""
    return _dbg.debug_breakpoint_disable(session_id, breakpoint_id)


@mcp.tool()
def debug_run_until_stop(session_id: str, timeout_ms: int = 30000) -> dict:
    """Continue the inferior until it stops (breakpoint / exception / exit)."""
    return _dbg.debug_run_until_stop(session_id, timeout_ms=timeout_ms)


@mcp.tool()
def debug_continue(session_id: str, timeout_ms: int = 30000) -> dict:
    """Continue execution."""
    return _dbg.debug_continue(session_id, timeout_ms=timeout_ms)


@mcp.tool()
def debug_step_over(session_id: str, timeout_ms: int = 30000) -> dict:
    """Step over the current line."""
    return _dbg.debug_step_over(session_id, timeout_ms=timeout_ms)


@mcp.tool()
def debug_step_into(session_id: str, timeout_ms: int = 30000) -> dict:
    """Step into the current call."""
    return _dbg.debug_step_into(session_id, timeout_ms=timeout_ms)


@mcp.tool()
def debug_step_out(session_id: str, timeout_ms: int = 30000) -> dict:
    """Step out of the current frame."""
    return _dbg.debug_step_out(session_id, timeout_ms=timeout_ms)


@mcp.tool()
def debug_interrupt(session_id: str) -> dict:
    """Asynchronously break a running inferior (CTRL_BREAK_EVENT)."""
    return _dbg.debug_interrupt(session_id)


# ---------------------------------------------------------------------------
# Inspection
# ---------------------------------------------------------------------------

@mcp.tool()
def debug_stack_get(session_id: str) -> dict:
    """Get the current thread's call stack."""
    return _dbg.debug_stack_get(session_id)


@mcp.tool()
def debug_frame_select(session_id: str, frame: int) -> dict:
    """Select a stack frame as the current context (for locals/eval)."""
    return _dbg.debug_frame_select(session_id, frame)


@mcp.tool()
def debug_scopes_get(session_id: str, name_filter: str = "") -> dict:
    """Get locals/arguments in the current frame."""
    return _dbg.debug_scopes_get(session_id, name_filter=name_filter)


@mcp.tool()
def debug_variable_expand(
    session_id: str,
    var_path: str,
    frame: int | None = None,
    max_children: int = 64,
    depth: int = 1,
) -> dict:
    """Drill down into a variable: children of structs/pointers (e.g. 'tile.assetUuid', 'this->layers')."""
    return _dbg.debug_variable_expand(
        session_id,
        var_path,
        frame=frame,
        max_children=max_children,
        depth=depth,
    )


@mcp.tool()
def debug_expression_eval(
    session_id: str,
    expression: str,
    frame: int | None = None,
    thread_id: int | None = None,
) -> dict:
    """Evaluate a C++ expression in the current frame."""
    return _dbg.debug_expression_eval(session_id, expression, frame=frame, thread_id=thread_id)


@mcp.tool()
def debug_memory_read(session_id: str, address: str, size: int, format: str = "bytes") -> dict:
    """Read a chunk of inferior memory."""
    return _dbg.debug_memory_read(session_id, address, size, format=format)


@mcp.tool()
def debug_registers_get(session_id: str) -> dict:
    """Read CPU registers for the current thread."""
    return _dbg.debug_registers_get(session_id)


@mcp.tool()
def debug_threads_get(session_id: str) -> dict:
    """List all threads in the inferior."""
    return _dbg.debug_threads_get(session_id)


@mcp.tool()
def debug_thread_select(session_id: str, thread_id: int) -> dict:
    """Switch the current thread."""
    return _dbg.debug_thread_select(session_id, thread_id)


# ---------------------------------------------------------------------------
# Extended
# ---------------------------------------------------------------------------

@mcp.tool()
def debug_modules_get(session_id: str, filter_text: str = "") -> dict:
    """List loaded modules (exe + DLLs), optionally filtered."""
    return _dbg.debug_modules_get(session_id, filter_text=filter_text)


@mcp.tool()
def debug_crash_report(session_id: str, frames_per_thread: int = 32, log_tail_lines: int = 60) -> dict:
    """One-shot crash snapshot: stop event, all thread stacks, project-frame scopes, registers, log tail."""
    return _dbg.debug_crash_report(
        session_id,
        frames_per_thread=frames_per_thread,
        log_tail_lines=log_tail_lines,
    )


@mcp.tool()
def debug_pdb_resolve(
    symbol: str = "",
    source_file: str = "",
    line: int = 0,
    exe_target: str = "",
    exe_path: str = "",
    build_dir: str = "",
) -> dict:
    """Resolve a symbol/file:line to an address via PDB (llvm-pdbutil fallback)."""
    return _dbg.debug_pdb_resolve(
        symbol=symbol,
        source_file=source_file,
        line=line,
        exe_target=exe_target,
        exe_path=exe_path,
        build_dir=build_dir,
    )


@mcp.tool()
def debug_crash_dump_analyze(
    dump_path: str,
    symbols: str = "",
    top_frames: int = 24,
    timeout_ms: int = 120000,
) -> dict:
    """Post-mortem: analyze a saved .dmp via ``cdb -z`` (``k``, exception code)."""
    return _dbg.debug_crash_dump_analyze(
        dump_path,
        symbols=symbols,
        top_frames=top_frames,
        timeout_ms=timeout_ms,
    )


@mcp.tool()
def debug_heap_stat(session_id: str) -> dict:
    """Heap statistics for the inferior (per-heap usage)."""
    return _dbg.debug_heap_stat(session_id)


@mcp.tool()
def debug_watchpoint_set(
    session_id: str,
    address: str = "",
    expression: str = "",
    access: str = "write",
    size: int = 4,
) -> dict:
    """Set a hardware data breakpoint (read / write / execute)."""
    return _dbg.debug_watchpoint_set(
        session_id,
        address=address,
        expression=expression,
        access=access,
        size=size,
    )


@mcp.tool()
def debug_process_find(name_query: str, limit: int = 20) -> dict:
    """Find running processes by name (for attach). Returns PIDs + match scores."""
    return _dbg.debug_process_find(name_query, limit=limit)


@mcp.tool()
def debug_process_terminate(process_id: int, expected_name: str) -> dict:
    """Terminate a process by id, guarded by an exact image-name match."""
    return _dbg.debug_process_terminate(process_id=process_id, expected_name=expected_name)


if __name__ == "__main__":
    mcp.run()
