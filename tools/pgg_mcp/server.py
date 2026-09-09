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
   и cache hits/misses в одном ответе; PNG потом читается как файл. По
   умолчанию кадр кропится до вьюпорта превью (без панели/графа); ``chrome="off"``
   растягивает превью на всё окно, ``zoom``/``distance`` управляют дальностью
   камеры (см. docstring инструмента).
4. ``pgg_probe`` (spec вида "house:schema" / "house:stats") — инспектор-записи.
5. Правка файла на диске → сразу ``pgg_render``/``pgg_probe``: F4 — сервер сам
   перечитывает файл и его импорты по mtime (ответ render/probe/export тогда
   содержит reloaded=true и load_diagnostics), явный ``pgg_load`` после правки
   не нужен. Сессионный кэш инвалидирует только downstream, повторный прогон
   заметно быстрее.

Прочее: ``pgg_params`` (значения @param, переживают load), ``pgg_export``
(OBJ узла), ``pgg_docs`` (карточка def'а: сигнатура + docstring),
``pgg_diff`` (fp-diff outputs против baseline'а), ``pgg_reference``
(side-by-side модель/референс + силуэтные метрики пропорций). Для ответа на
«правка попала туда?» — ``pgg_render(node, compare="prev")``: diff-метрики и
PNG-разница с предыдущим кадром того же вида.
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
    preview:{target,has_value}, profile:[{name,ms,field_evals,cache_hit}] (top-20
    binding'ов последнего прогона по эксклюзивному ms) + profile_total_ms,
    uptime_s}. Пример: pgg_status().
    """
    return _call("status")


@mcp.tool()
def pgg_load(path: Optional[str] = None, source: Optional[str] = None,
             lib_roots: Optional[list[str]] = None,
             snapshot: Optional[bool] = None) -> dict:
    """Загрузить .pgg-файл (ровно один из path/source обязателен).

    path — путь до .pgg (относительно cwd viewer'а = корня репо, или абсолютный);
    source — текст файла целиком (пишется в tmp/pgg_rpc_source/src_N.pgg, его
    каталог становится неявным import root); lib_roots — доп. корни импортов.
    snapshot=true дополнительно прогоняет outputs и записывает их фингерпринты
    как baseline для pgg_diff (в ответе snapshot=true; при ошибках файла
    baseline не пишется, snapshot=false). Без snapshot=true снимок pgg_diff
    создаст первый вызов pgg_diff (baseline_created=true); явный pgg_load
    снимок СБРАСЫВАЕТ (новый контекст документа).
    Ответ — СТАТИЧЕСКАЯ проверка без прогона графа (невалидный файл отвечает
    за миллисекунды): {diagnostics:[{code,line,col,warning,message}],
    has_errors, ms, path}.
    После pgg_load(path=...) правки файла (и его импортов из lib/) на диске
    подхватываются АВТОМАТИЧЕСКИ следующим pgg_render/pgg_probe/pgg_export
    (F4, авто-reload по mtime; в ответе reloaded=true + load_diagnostics) —
    pgg_load нужен только для source, смены файла или немедленной статической
    проверки. Исключение: файл, загруженный через source (temp-файл), не
    отслеживается — новый текст передаётся новым pgg_load(source=...).
    Пример: pgg_load(path="resources/pgg/cottage.pgg") → has_errors=false.
    """
    return _call("load", {"path": path, "source": source, "lib_roots": lib_roots,
                          "snapshot": snapshot})


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
               ortho: Optional[str] = None, wire: Optional[bool] = None,
               frame: Optional[str] = None, chrome: Optional[str] = None,
               zoom: Optional[float] = None, distance: Optional[float] = None,
               compare: Optional[str] = None) -> dict:
    """Синхронный прогон узла + PNG-кадр превью + статистика в одном ответе.

    node — имя binding/output'а; out — куда писать PNG (по умолчанию
    tmp/pgg_rpc_shots/shot_N.png, каталог создаётся).
    Кадр (F1): frame — "preview"|"window", по умолчанию "preview": PNG
    кропится до вьюпорта превью-панели (физические пиксели буфера; панель,
    граф и строка тулбара панели в кадр НЕ попадают); "window" — весь кадр
    окна. chrome — "on"|"off": off рисует один кадр без панели и графа —
    превью-панель растягивается на всё окно (больше пикселей модели; окно
    программно не ресайзится, это максимум разрешения). size — при
    frame=preview целевой размер кропа в ПИКСЕЛЯХ PNG (центрируется на
    вьюпорте; окно не ресайзится, поэтому запрос больше вьюпорта клампится
    к фактическому rect'у и в ответе появляется size_clamped=true); при
    frame=window — легаси [w, h] превью-панели в пунктах (двигает сплиттер,
    не ресайзит окно).
    Камера: orbit — [yaw, pitch] или [yaw, pitch, zoom]; yaw/pitch в градусах.
    Третий компонент orbit и именованный zoom — МНОЖИТЕЛЬ fit-дистанции
    (fit = radius*2.6*zoom): 1 = вписать цель в кадр, 0.5 = вдвое ближе,
    3 = втрое дальше; zoom перекрывает orbit[2], когда заданы оба.
    distance — дистанция камеры от центра орбиты в МЕТРАХ (перекрывает
    zoom/orbit[2]; применяется после резолва target, т.е. от центра цели;
    переживает рефиты как эквивалентный множитель fit-дистанции).
    highlight — имя группы подсветки; shading — "auto"|"flat"|"smooth";
    colors — vertex colors on/off.
    Сравнение с предыдущим кадром (F3): compare="prev" — в ответ добавляется
    compare:{available, changed_pct, change_bbox_px:[x0,y0,x1,y1] (x1/y1
    exclusive), diff_png} — diff с ПРЕДЫДУЩИМ кадром ТОГО ЖЕ вида (узел +
    эффективная камера + frame/chrome; значения params в ключ НЕ входят —
    именно правку параметра/файла compare и показывает). available=false с
    reason "no previous frame" (первый кадр вида) или "size mismatch (...)";
    diff_png — tmp/pgg_rpc_shots/diff_N.png: затемнённый новый кадр, изменённые
    пиксели подсвечены magenta (порог 8/255 на канал). Хранится ОДИН последний
    кадр — его заменяет каждый успешный render (с compare или без).
    Типовой цикл правки: pgg_render(node) → правка файла/params →
    pgg_render(node, compare="prev") → changed_pct/bbox/diff_png показывают,
    куда правка попала на экране.
    Нацеливание (A2): target — "x,y,z" | "group:<имя>" | "binding:<путь>"
    (центр орбиты; group — bbox группы свежего прогона, имя с доменом
    "faces:roof" или голое; "" снимает ранее заданный target; без target
    сохраняется прежний); fit — "all"|"target" (по умолчанию target, если
    задан target); ortho — "front"|"side"|"top"|"off" (орто-проекция по оси,
    ракурс снапится: front = камера на +Z, side = на +X, top = сверху);
    wire — тёмный wireframe-оверлей рёбер поверх шейдинга.
    Ответ data: {path, width, height (фактические размеры PNG), frame, chrome,
    size_clamped?, node, stats:{kind, pts, tri, bbox, groups, ms},
    camera:{center, radius, distance}, cache:{hits,misses}, reloaded,
    load_diagnostics?} — PNG потом читается как файл. reloaded=true +
    load_diagnostics означают, что перед прогоном сработал авто-reload по
    mtime (правка файла/импортов на диске, F4).
    Пример: pgg_render(node="house", target="group:faces:roof", ortho="front",
    chrome="off").
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
                            "wire": wire, "frame": frame, "chrome": chrome,
                            "zoom": zoom, "distance": distance,
                            "compare": compare})


@mcp.tool()
def pgg_reference(image: str, node: str, ortho: Optional[str] = None,
                  orbit: Optional[list[float]] = None, zoom: Optional[float] = None,
                  size: Optional[list[float]] = None) -> dict:
    """Модель рядом с референсом (F3): side-by-side PNG + силуэтные метрики.

    Прогон узла (как pgg_render) снимается орто-кадром (по умолчанию
    ortho="front"; "side"|"top"|"off" — perspective; orbit/zoom — те же
    множители, что у pgg_render; ракурс детерминирован: target сбрасывается,
    fit=all по всей сцене) и склеивается в один PNG с референсом: слева кадр
    модели, справа референс, ресайзнутый bilinear до высоты кадра (аспект
    сохраняется), между ними разделитель 4px. Результат —
    tmp/pgg_rpc_shots/ref_N.png (путь в ответе), читается как файл.
    size — как у pgg_render frame=preview: целевой размер кропа модели в
    пикселях (кламп к вьюпорту, size_clamped=true в ответе).
    Силуэтные метрики (фон модели — известный clear-цвет превью; фон
    референса — мажоритарный цвет 4 углов, ответ содержит
    reference_background): model/reference = {bbox_frac:[x0,y0,x1,y1] силуэта
    в долях кадра, w_over_h — отношение ширины bbox к высоте, rows — 10
    горизонтальных полос сверху вниз, в каждой средняя доля ширины силуэта,
    empty}. По rows модель vs референс сверяются пропорции без ручного счёта
    пикселей; bbox_frac показывает сдвиг/масштаб силуэта.
    Ответ data: {path, width, height, node, model:{...}, reference:{...},
    reference_background:[r,g,b], ms, stats:{...}, camera:{...},
    cache:{hits,misses}, reloaded, load_diagnostics?}.
    Ошибки: image не читается — invalid_input; headless/без UI —
    no_frame_loop; прогон — run_failed/run_errors как у render.
    Пример: pgg_reference(image="tmp/bug.png", node="house", ortho="front").
    """
    return _call("reference", {"image": image, "node": node, "ortho": ortho,
                               "orbit": orbit, "zoom": zoom, "size": size})


@mcp.tool()
def pgg_probe(spec: str) -> dict:
    """Пробник-инспектор узла: probe-only прогон (outputs не считаются).

    spec — "путь:инспектор[параметры]", например "house:schema" (структура
    значения) или "house:stats" (числа по доменам, bbox). Инспекторы:
    schema/stats/coverage/table — L0–L2; sample/slice — поле в точках и срез
    (sdf и geo); check — здоровье меша; lattice[voxel=0.05] — решётка
    mesh_from_sdf и предупреждения о гранях у плоскостей решётки (только sdf);
    table[where=<expr>,limit=N] / find[where=<expr>] — фильтрация/сводка по
    предикату над точками ("house:find[where=@ao < 0.9]"). Контракты — спека
    §9.6. Ответ data:
    {records:[{origin,path,inspector,text}], diagnostics, has_errors, ms,
    cache:{hits,misses}, reloaded, load_diagnostics?} (reloaded=true — перед
    прогоном сработал авто-reload по mtime, F4).
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
    cache:{hits,misses}, reloaded, load_diagnostics?} (reloaded — авто-reload
    по mtime, F4).
    Пример: pgg_export(node="house", obj_path="tmp/house.obj").
    """
    return _call("export", {"node": node, "obj_path": obj_path})


@mcp.tool()
def pgg_diff(update: Optional[bool] = None) -> dict:
    """Diff outputs текущего файла против снимка fingerprints (C2, fp-level).

    Сервер хранит снимок структурных фингерпринтов outputs последнего
    baseline'а. Первый вызов (или вызов после явного pgg_load) снимок
    СОЗДАЁТ и отвечает baseline_created=true (сравнения нет). Дальнейшие
    вызовы прогоняют outputs текущего файла и сравнивают: outputs:[{name,
    status: identical|changed|added|removed|skipped, fingerprint_prev,
    fingerprint_now}], identical — сводный вердикт (skipped — sdf/field без
    структурного fp, на вердикт не влияет; ΔP не считается — это fp-level
    diff, таблица изменений — `PggTool diff` в CLI). Правка файла на диске
    подхватывается авто-reload'ом ВНУТРИ diff (F4, reloaded=true) — снимок
    она НЕ сбрасывает. Снимок обновляется только update=true
    (snapshot_updated=true в ответе) или явным pgg_load — он снимок сбрасывает
    (pgg_load(..., snapshot=True) записывает свежий baseline сразу, иначе его
    запишет первый pgg_diff с baseline_created=true).
    Ответ data: {baseline_created?|identical, outputs, snapshot_updated?,
    reloaded, load_diagnostics?, ms, cache:{hits,misses}}.
    Пример: pgg_diff() после правки cottage.pgg → changed по затронутым
    outputs; pgg_diff(update=True) — принять новое состояние как baseline.
    """
    return _call("diff", {"update": update})


@mcp.tool()
def pgg_docs(symbol: str) -> dict:
    """Карточка def'а или builtin'а (как `PggTool docs` / `docs builtin`).

    symbol — имя def'а; qualified имена (module.symbol) тоже работают через
    module closure загруженного файла. Для builtin'а — `symbol="builtin:<name>"`
    (например "builtin:clip"): сигнатура из живого реестра + группа + summary
    + пример; работает и без загруженного файла.
    Ответ data: {symbol, kind, signature, docstring} для def'а или
    {symbol, kind, name, signature, group, summary, example} для builtin'а;
    если символ не найден — ok=false с kind="not_found".
    Пример: pgg_docs(symbol="make_roof"), pgg_docs(symbol="builtin:set_position").
    """
    return _call("docs", {"symbol": symbol})


if __name__ == "__main__":
    mcp.run()
