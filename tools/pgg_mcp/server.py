"""neverwhere PGG MCP server — agent tooling over the PggViewer RPC.

Stdio transport. Started by ``tools/run_pgg_mcp_server.sh`` (macOS/Linux) or
``tools/run_pgg_mcp_server.ps1`` (Windows) via
``python -m tools.pgg_mcp.server``.

Thin proxy to the PggViewer RPC server (raw TCP + line-delimited JSON on
127.0.0.1:9878, see ``src/apps/PggViewer/ViewerRpcServer.cpp`` and
``docs/pgg/viewer_rpc.md``). Unlike the editor MCP this server AUTO-STARTS
``PggViewer --serve`` when the port does not answer. Every response keeps
the RPC envelope (``{"ok": true, "data": ...}`` / ``{"ok": false, "error"}``).
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any, Optional

from mcp.server.fastmcp import FastMCP

from tools.pgg_mcp.rpc_client import PggRpcClient, PggRpcError, port_open, wait_for_port

_HOST = "127.0.0.1"
_PORT = 9878

_INSTRUCTIONS = """neverwhere PGG MCP — итеративный цикл отладки .pgg-графов через PggViewer.

Типовой цикл «правка → картинка + числа»:

1. ``pgg_status`` — viewer жив (поднимается автоматически при первом вызове).
2. ``pgg_load`` (path до .pgg или source целиком) — статическая проверка без
   прогона: диагностики за миллисекунды, has_errors=false → файл валиден.
3. ``pgg_render`` (node) — PNG кадра + статы ({kind, pts, tri, bbox, groups, ms})
   и cache hits/misses в одном ответе; PNG потом читается как файл.
4. ``pgg_probe`` (spec вида "house:schema" / "house:stats") — инспектор-записи.
5. Правка файла → снова ``pgg_load`` + ``pgg_render``: сессионный кэш
   инвалидирует только downstream, повторный прогон заметно быстрее.

Прочее: ``pgg_params`` (значения @param, переживают load), ``pgg_export``
(OBJ узла), ``pgg_docs`` (карточка def'а: сигнатура + docstring).
"""

mcp = FastMCP("neverwhere-pgg", instructions=_INSTRUCTIONS)

# Lazily-connected persistent TCP client (the MCP stdio server is long-lived).
_client: Optional[PggRpcClient] = None
# Auto-started viewer process (kept so it dies with the MCP server).
_viewer_proc: Optional[subprocess.Popen] = None


def _repo_root() -> str:
    env = os.environ.get("NEVERWHERE_REPO_ROOT")
    if env:
        return env
    return str(Path(__file__).resolve().parent.parent.parent)


def _find_viewer_binary(root: str) -> Optional[str]:
    """First existing PggViewer binary wins; Release is much faster than Debug."""
    candidates = []
    env = os.environ.get("NEVERWHERE_PGG_VIEWER")
    if env:
        candidates.append(env)
    candidates += [
        "_int_linux_release/src/apps/PggViewer/Release/PggViewer",
        "_int_linux/src/apps/PggViewer/Debug/PggViewer",
        "_int_clion_release/src/apps/PggViewer/Release/PggViewer",
        "_int_clion/src/apps/PggViewer/Debug/PggViewer",
        "_intermediate_64/src/apps/PggViewer/Debug/PggViewer",
    ]
    for c in candidates:
        p = c if os.path.isabs(c) else os.path.join(root, c)
        for cand in (p, p + ".exe"):
            if os.path.isfile(cand):
                return cand
    return None


def _ensure_viewer() -> Optional[str]:
    """Make sure a PggViewer --serve answers on the RPC port; start one if not.

    Returns None on success, an error message otherwise.
    """
    global _viewer_proc
    if port_open(_HOST, _PORT):
        return None
    root = _repo_root()
    viewer = _find_viewer_binary(root)
    if viewer is None:
        return ("PggViewer binary not found (set NEVERWHERE_PGG_VIEWER or build "
                "PggViewer into one of the standard build dirs)")
    cmd = [viewer, "--serve"]
    if (sys.platform.startswith("linux") and not os.environ.get("DISPLAY")
            and shutil.which("xvfb-run")):
        cmd = ["xvfb-run", "-a"] + cmd
    # Never inherit stdout/stderr: child output would corrupt the MCP stdio
    # transport; log to a file under tmp/ instead.
    log_dir = Path(root) / "tmp"
    log_dir.mkdir(parents=True, exist_ok=True)
    log = open(log_dir / "pgg_viewer_serve.log", "ab", buffering=0)
    _viewer_proc = subprocess.Popen(cmd, cwd=root, stdout=log, stderr=log)
    if wait_for_port(_HOST, _PORT, timeout_s=30.0, step_s=0.5):
        return None
    if _viewer_proc.poll() is not None:
        return f"PggViewer exited early (code {_viewer_proc.returncode}); see tmp/pgg_viewer_serve.log"
    return "PggViewer did not open the RPC port within 30 s; see tmp/pgg_viewer_serve.log"


def _call(op: str, args: Optional[dict[str, Any]] = None) -> dict:
    """Send one RPC op; auto-start the viewer, reconnect once on transport failure.

    Server-side errors (ok=false) are returned as-is, not retried.
    """
    global _client
    start_error = _ensure_viewer()
    if start_error:
        return {"ok": False, "error": {"kind": "unreachable", "message": start_error}}

    payload: dict[str, Any] = {"op": op}
    if args:
        # Drop unset optional args so the server applies its defaults.
        payload["args"] = {k: v for k, v in args.items() if v is not None}

    last_error: Optional[Exception] = None
    for _ in range(2):
        try:
            if _client is None:
                _client = PggRpcClient(host=_HOST, port=_PORT)
            return _client.call(**payload)
        except PggRpcError as e:
            return {"ok": False, "error": {"kind": e.kind, "message": e.message}}
        except (ConnectionError, OSError) as e:
            last_error = e
            if _client is not None:
                _client.close()
            _client = None
            # The viewer may have died — give the auto-start one more chance.
            start_error = _ensure_viewer()
            if start_error:
                last_error = start_error
                break
    return {
        "ok": False,
        "error": {
            "kind": "unreachable",
            "message": f"PggViewer RPC unreachable: {last_error}",
        },
    }


@mcp.tool()
def pgg_status() -> dict:
    """Живость viewer'а и состояние сессии.

    Ответ data: {file, params, cache:{size,capacity,hits,misses},
    preview:{target,has_value}, uptime_s}. Пример: pgg_status().
    """
    return _call("status")


@mcp.tool()
def pgg_load(path: Optional[str] = None, source: Optional[str] = None,
             lib_roots: Optional[list[str]] = None) -> dict:
    """Загрузить .pgg-файл (ровно один из path/source обязателен).

    path — путь до .pgg (относительно cwd viewer'а = корня репо, или абсолютный);
    source — текст файла целиком (пишется в tmp/pgg_rpc_source/src_N.pgg, его
    каталог становится неявным import root); lib_roots — доп. корни импортов.
    Ответ — СТАТИЧЕСКАЯ проверка без прогона графа (невалидный файл отвечает
    за миллисекунды): {diagnostics:[{code,line,col,warning,message}],
    has_errors, ms, path}.
    Пример: pgg_load(path="resources/pgg/cottage.pgg") → has_errors=false.
    """
    return _call("load", {"path": path, "source": source, "lib_roots": lib_roots})


@mcp.tool()
def pgg_params(params: dict[str, Any]) -> dict:
    """Установить значения @param-параметров графа (переживают pgg_load).

    params — словарь {имя: значение}; значения — числа/строки/bool или массив
    (массив сериализуется в вектор "(x, y, z)"). Неизвестные имена возвращаются
    в поле unknown. Ответ data: {params:{...текущие...}, unknown:[...]}.
    Пример: pgg_params({"stories": 2, "seed": 42}).
    """
    return _call("params", params)


@mcp.tool()
def pgg_render(node: str, out: Optional[str] = None, orbit: Optional[list[float]] = None,
               highlight: Optional[str] = None, shading: Optional[str] = None,
               colors: Optional[bool] = None, size: Optional[list[float]] = None,
               target: Optional[str] = None, fit: Optional[str] = None,
               ortho: Optional[str] = None, wire: Optional[bool] = None) -> dict:
    """Синхронный прогон узла + PNG-кадр превью + статистика в одном ответе.

    node — имя binding/output'а; out — куда писать PNG (по умолчанию
    tmp/pgg_rpc_shots/shot_N.png, каталог создаётся); orbit — [yaw, pitch]
    или [yaw, pitch, zoom]; highlight — имя группы подсветки; shading —
    "auto"|"flat"|"smooth"; colors — vertex colors on/off; size — [w, h]
    превью-панели (двигает сплиттер, не ресайзит окно).
    Камера (A2): target — "x,y,z" | "group:<имя>" | "binding:<путь>" (центр
    орбиты; group — bbox группы свежего прогона, имя с доменом "faces:roof"
    или голое; "" снимает ранее заданный target; без target сохраняется
    прежний); fit — "all"|"target" (по умолчанию target, если задан target);
    ortho — "front"|"side"|"top"|"off" (орто-проекция по оси, ракурс снапится:
    front = камера на +Z, side = на +X, top = сверху); wire — тёмный
    wireframe-оверлей рёбер поверх шейдинга.
    Ответ data: {path, width, height, node, stats:{kind, pts, tri, bbox,
    groups, ms}, camera:{center, radius, distance}, cache:{hits,misses}} —
    PNG потом читается как файл.
    Пример: pgg_render(node="house", target="group:faces:roof", ortho="front").
    Грабли: прогон синхронный — тяжёлый граф блокирует окно viewer'а на всё
    время рендера; требуется UI-режим (с --no-ui — ошибка no_frame_loop);
    при залоченном/спящем экране macOS кадры не тикают, поэтому первый
    render после простоя может ждать пробуждения дисплея (viewer сам держит
    beginActivity против idle-sleep, но залоченный экран это не лечит).
    """
    return _call("render", {"node": node, "out": out, "orbit": orbit,
                            "highlight": highlight, "shading": shading,
                            "colors": colors, "size": size,
                            "target": target, "fit": fit, "ortho": ortho,
                            "wire": wire})


@mcp.tool()
def pgg_probe(spec: str) -> dict:
    """Пробник-инспектор узла: probe-only прогон (outputs не считаются).

    spec — "путь:инспектор[параметры]", например "house:schema" (структура
    значения) или "house:stats" (числа по доменам, bbox). Ответ data:
    {records:[{origin,path,inspector,text}], diagnostics, has_errors, ms,
    cache:{hits,misses}}.
    Пример: pgg_probe(spec="house:schema").
    """
    return _call("probe", {"spec": spec})


@mcp.tool()
def pgg_export(node: str, obj_path: str) -> dict:
    """Экспорт геометрии узла в OBJ (pull + pgg::writeObj).

    node — имя binding/output'а; obj_path — куда писать .obj (каталог
    создаётся). instances экспортируются через realize (realized=true в
    ответе); sdf-значения не экспортируются — ошибка no_geometry («mesh them
    with mesh_from_sdf»). Ответ data: {path, realized, stats:{...},
    cache:{hits,misses}}.
    Пример: pgg_export(node="house", obj_path="tmp/house.obj").
    """
    return _call("export", {"node": node, "obj_path": obj_path})


@mcp.tool()
def pgg_docs(symbol: str) -> dict:
    """Карточка def'а: сигнатура + docstring (как `PggTool docs`).

    symbol — имя def'а; qualified имена (module.symbol) тоже работают через
    module closure загруженного файла. Ответ data: {symbol, signature,
    docstring}; если def не найден — ok=false с kind="not_found".
    Пример: pgg_docs(symbol="make_roof").
    """
    return _call("docs", {"symbol": symbol})


if __name__ == "__main__":
    mcp.run()
