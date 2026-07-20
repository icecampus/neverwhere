"""Smoke test for the macOS LLDB debug backend (lldb_backend + lldb_worker).

Runs against the pure backend API (the same functions the MCP server wraps)
and prints ``TEST PASS`` / ``TEST FAIL`` markers like the project smoke tests.

Scenarios:
- default: ``/tmp/nw_lldb_crash`` — a nullptr-deref binary. Covers
  self_check, launch, source breakpoint, run_until_stop, stack/scopes/eval,
  the SIGSEGV stop (surfaced as Mach EXC_BAD_ACCESS with signal_equivalent
  SIGSEGV) and session teardown.
- ``--real-app``: EpicGameClient — break_on_start, breakpoint on ``main``,
  DWARF file/line visibility in stack frames, teardown.

Run with the repo venv python: ``tools/debug_mcp/.venv/bin/python``.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path
from typing import Any

REPO_ROOT = Path(__file__).resolve().parents[2]
if str(REPO_ROOT) not in sys.path:
    sys.path.insert(0, str(REPO_ROOT))

from tools.debug_mcp import lldb_backend as dbg  # noqa: E402

CRASH_SRC = Path("/tmp/nw_lldb_crash.cpp")
CRASH_EXE = Path("/tmp/nw_lldb_crash")

CRASH_CODE = """int compute(int x) { return x * 2; }
int main() {
    int value = compute(21);
    volatile int* p = nullptr;
    return *p + value;
}
"""

_RESULTS: list[tuple[str, bool, str]] = []


def check(name: str, cond: bool, extra: str = "") -> bool:
    _RESULTS.append((name, bool(cond), extra))
    print(f"{'TEST PASS' if cond else 'TEST FAIL'}: {name}" + (f" ({extra})" if extra else ""))
    return bool(cond)


def _sig_name(stop_event: dict[str, Any]) -> str | None:
    signal_info = stop_event.get("signal") or {}
    if signal_info.get("name"):
        return str(signal_info["name"])
    exception_info = stop_event.get("exception") or {}
    equivalent = exception_info.get("signal_equivalent")
    return str(equivalent) if equivalent else None


def scenario_crash_binary() -> bool:
    print("--- scenario: /tmp crash binary (nullptr deref -> SIGSEGV) ---")
    CRASH_SRC.write_text(CRASH_CODE, encoding="utf-8")
    compile_result = subprocess.run(
        ["clang++", "-g", "-O0", "-std=c++17", str(CRASH_SRC), "-o", str(CRASH_EXE)],
        capture_output=True,
        text=True,
        check=False,
    )
    ok = check("compile crash binary", compile_result.returncode == 0, compile_result.stderr[:300])
    if not ok:
        return False

    compute_line = next(
        i for i, line in enumerate(CRASH_CODE.splitlines(), start=1) if "return x * 2" in line
    )

    self_check = dbg.debug_self_check()
    check("debug_self_check ok", self_check.get("ok"), "; ".join(self_check["data"].get("issues", [])))

    session = dbg.debug_session_start(
        exe=str(CRASH_EXE), break_on_start=False, startup_timeout_ms=15000
    )
    ok = check("session_start launch", session.get("ok"), session.get("summary", ""))
    if not ok:
        print(session)
        return False
    session_id = str(session.get("session_id"))

    bp = dbg.debug_breakpoint_set(
        session_id, source_file=str(CRASH_SRC), line=compute_line
    )
    check(
        "breakpoint_set bound",
        bp.get("ok") and bp["data"].get("binding_status") == "bound",
        str(bp["data"].get("resolved_location") if bp.get("ok") else bp.get("error")),
    )

    run = dbg.debug_run_until_stop(session_id, timeout_ms=15000)
    stop_event = (run.get("data") or {}).get("stop_event") or {}
    check(
        "run_until_stop hits breakpoint",
        run.get("ok") and stop_event.get("reason") == "breakpoint",
        f"reason={stop_event.get('reason')}",
    )

    stack = dbg.debug_stack_get(session_id)
    functions = [frame.get("function", "") for frame in (stack.get("data") or {}).get("frames", [])]
    check("stack_get shows compute()", stack.get("ok") and any("compute" in f for f in functions), str(functions))

    scopes = dbg.debug_scopes_get(session_id)
    args_found = {
        (item.get("name"), item.get("value")) for item in (scopes.get("data") or {}).get("args", [])
    }
    check("scopes_get sees x=21", scopes.get("ok") and ("x", "21") in args_found, str(args_found))

    # NOTE: compute(21) -> x == 21, so x * 10 == 210 (the task brief said 420,
    # which does not match x=21; 210 is the correct expected value).
    evaluation = dbg.debug_expression_eval(session_id, "x * 10")
    check(
        "expression_eval 'x * 10' == 210",
        evaluation.get("ok") and str((evaluation.get("data") or {}).get("value")) == "210",
        str((evaluation.get("data") or {}).get("value")),
    )

    run2 = dbg.debug_run_until_stop(session_id, timeout_ms=15000)
    stop_event2 = (run2.get("data") or {}).get("stop_event") or {}
    sig = _sig_name(stop_event2)
    check(
        "run_until_stop stops on SIGSEGV",
        run2.get("ok")
        and stop_event2.get("reason") in ("signal", "exception")
        and sig == "SIGSEGV",
        f"reason={stop_event2.get('reason')} sig={sig} desc={stop_event2.get('stop_description')}",
    )

    stack2 = dbg.debug_stack_get(session_id)
    frames2 = (stack2.get("data") or {}).get("frames", [])
    check(
        "stack_get at crash is non-empty",
        stack2.get("ok") and len(frames2) > 0,
        str([frame.get("function") for frame in frames2]),
    )

    stop = dbg.debug_session_stop(session_id, kill_process=True)
    check("session_stop kills inferior", stop.get("ok") and (stop.get("data") or {}).get("killed"), stop.get("summary", ""))
    return all(result[1] for result in _RESULTS)


def scenario_real_app() -> bool:
    print("--- scenario: EpicGameClient (break_on_start + breakpoint on main) ---")
    _RESULTS.clear()

    from tools.debug_mcp.exe_resolver import resolve_exe
    from tools.debug_mcp.repo_root import repo_root

    resolution = resolve_exe(repo_root(), exe_target="EpicGameClient")
    exe_path = str(resolution.exe)
    check(
        "resolve EpicGameClient exe",
        resolution.exe.is_file(),
        exe_path,
    )
    if not resolution.exe.is_file():
        return False

    self_check = dbg.debug_self_check()
    check("debug_self_check ok", self_check.get("ok"), "; ".join(self_check["data"].get("issues", [])))

    main_cpp = REPO_ROOT / "src" / "apps" / "EpicGameClient" / "main.cpp"
    main_line = next(
        i
        for i, line in enumerate(main_cpp.read_text(encoding="utf-8").splitlines(), start=1)
        if line.startswith("int main(")
    )
    print(f"EpicGameClient main at {main_cpp}:{main_line}")

    session = dbg.debug_session_start(
        exe=exe_path,
        cwd=str(REPO_ROOT),
        break_on_start=True,
        startup_timeout_ms=30000,
    )
    ok = check("session_start break_on_start", session.get("ok"), session.get("summary", ""))
    if not ok:
        print(session)
        return False
    session_id = str(session.get("session_id"))
    initial = ((session.get("data") or {}).get("last_stop_event") or {}).get("reason")
    check("stopped at entry", initial in ("initial_stop", "signal", "exec"), f"reason={initial}")

    bp = dbg.debug_breakpoint_set(session_id, source_file=str(main_cpp), line=main_line)
    check(
        "breakpoint on main bound",
        bp.get("ok") and bp["data"].get("binding_status") == "bound",
        str(bp["data"].get("resolved_location") if bp.get("ok") else bp.get("error")),
    )

    run = dbg.debug_run_until_stop(session_id, timeout_ms=30000)
    stop_event = (run.get("data") or {}).get("stop_event") or {}
    check(
        "run_until_stop reaches main",
        run.get("ok") and stop_event.get("reason") == "breakpoint",
        f"reason={stop_event.get('reason')}",
    )

    stack = dbg.debug_stack_get(session_id)
    frames = (stack.get("data") or {}).get("frames", [])
    main_frames = [
        frame
        for frame in frames
        if frame.get("function") and "main" in frame.get("function", "")
    ]
    dwarf_ok = bool(
        main_frames
        and main_frames[0].get("file", "").endswith("main.cpp")
        and main_frames[0].get("line", 0) > 0
    )
    check(
        "DWARF file/line visible in frames",
        stack.get("ok") and dwarf_ok,
        str(
            {
                "function": main_frames[0].get("function"),
                "file": main_frames[0].get("file"),
                "line": main_frames[0].get("line"),
                "is_project_frame": main_frames[0].get("is_project_frame"),
            }
            if main_frames
            else frames[:3]
        ),
    )
    project_frame = (stack.get("data") or {}).get("first_project_frame")
    check(
        "first_project_frame under repo root",
        bool(project_frame) and str(REPO_ROOT) in str(project_frame.get("file", "")),
        str(project_frame),
    )

    stop = dbg.debug_session_stop(session_id, kill_process=True)
    check("session_stop kills GUI client", stop.get("ok"), stop.get("summary", ""))
    return all(result[1] for result in _RESULTS)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--real-app", action="store_true", help="run the EpicGameClient scenario")
    args = parser.parse_args()

    passed = scenario_real_app() if args.real_app else scenario_crash_binary()
    failures = [name for name, ok, _ in _RESULTS if not ok]
    print(f"--- smoke result: {len(_RESULTS) - len(failures)}/{len(_RESULTS)} checks passed ---")
    if failures:
        print("TEST FAIL: " + ", ".join(failures))
        return 1
    print("TEST PASS: smoke scenario finished OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
