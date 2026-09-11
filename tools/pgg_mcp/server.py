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
3. ``pgg_render`` (node или view) — PNG кадра + статы + ``render_state`` в одном
   ответе. Незаданные args сбрасываются в дефолты (липкие только orbit/distance).
   Именованный вид — ``view`` из ``<stem>.views.json``. ``chrome="off"``, ``zoom``/
   ``distance`` — дальность камеры. Перед grep по спеке: ``pgg_docs("clip")``.
4. ``pgg_probe`` (spec вида "house:schema" / "house:bbox[group=stone]") —
   инспектор-записи; зазор — ``pgg_measure``.
5. Правка файла на диске → сразу ``pgg_render``/``pgg_probe``: F4 — сервер сам
   перечитывает файл и его импорты по mtime (ответ содержит reloaded=true),
   явный ``pgg_load`` после правки не нужен.

Прочее: ``pgg_params``, ``pgg_export``, ``pgg_docs`` (def, иначе builtin без
префикса; поиск по каталогу — ``PggTool docs builtins | rg -i …``),
``pgg_diff``, ``pgg_reference``, ``pgg_views``, ``pgg_contact_sheet``.
``pgg_render(..., compare="prev"|"baseline")`` — пиксельный diff; ``png=false``
отдаёт статы без записи кадра.
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
def pgg_render(node: Optional[str] = None, out: Optional[str] = None, orbit: Optional[list[float]] = None,
               highlight: Optional[str] = None, shading: Optional[str] = None,
               colors: Optional[bool] = None, size: Optional[list[float]] = None,
               target: Optional[str] = None, fit: Optional[str] = None,
               ortho: Optional[str] = None, wire: Optional[bool] = None,
               frame: Optional[str] = None, chrome: Optional[str] = None,
               zoom: Optional[float] = None, distance: Optional[float] = None,
               compare: Optional[str] = None, view: Optional[str] = None,
               png: Optional[bool] = None, save_baseline: Optional[bool] = None) -> dict:
    """Синхронный прогон узла + PNG-кадр превью + статистика в одном ответе.

    node — имя binding/output'а (можно опустить, если view задаёт node);
    view — имя из <stem>.views.json рядом с загруженным .pgg; явные args перекрывают поля вида.
    out — куда писать PNG (по умолчанию tmp/pgg_rpc_shots/shot_N.png).
    png=false — прогон + статы + render_state + compare без записи основного PNG и без path.
    Кадр (F1): frame — "preview"|"window", по умолчанию "preview"; chrome — "on"|"off".
    Камера: orbit — [yaw, pitch] или [yaw, pitch, zoom]; zoom — множитель fit-дистанции;
    distance — метры от центра орбиты (липкие вместе с yaw/pitch). Остальные параметры
    RPC-render **stateless**: незаданный wire/ortho/target/highlight/shading/colors/chrome/zoom
    сбрасывается в дефолт (wire=false, ortho=off, target="", chrome=on, zoom=1).
    Нацеливание: target — "x,y,z" | "group:<имя>" | "group:<grp>@<binding>" |
    "binding:<путь>" (путь как у пробника, '/' → '.'). Неразрешённый — target_unresolved.
    compare — "prev"|"baseline"; save_baseline=true запоминает кадр вида. load сбрасывает
    visual baseline. Ответ: path?, width, height, stats, camera (центр/radius/distance +
    target_bbox?), render_state:{wire,chrome,ortho,target,zoom,fit}, compare?.
    Пример: pgg_render(view="front", wire=False, chrome="off").
    """
    return _call("render", {"node": node, "out": out, "orbit": orbit,
                            "highlight": highlight, "shading": shading,
                            "colors": colors, "size": size,
                            "target": target, "fit": fit, "ortho": ortho,
                            "wire": wire, "frame": frame, "chrome": chrome,
                            "zoom": zoom, "distance": distance,
                            "compare": compare, "view": view, "png": png,
                            "save_baseline": save_baseline})


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
    Пример: pgg_reference(image="tmp/reference.png", node="house", ortho="front").
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
    mesh_from_sdf; bbox[group=<grp>] — min/max/center/size группы (без group —
    весь geo); gap[a=group:…, b=group:…, axis=x|y|z] — зазор bbox по оси
    (перекрытие отрицательное). table[where=<expr>,limit=N] / find[where=<expr>].
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

    symbol — имя def'а загруженного файла или имя builtin'а БЕЗ обязательного
    префикса ``builtin:``: сначала ищется def, при промахе — реестр билтинов.
    Def побеждает одноимённый builtin. Явный ``builtin:<name>`` всегда реестр.
    Имя неизвестно — ``PggTool docs builtins | rg -i …`` (весь каталог).
    Ответ data: {symbol, kind, signature, docstring} для def'а или
    {symbol, kind, name, signature, group, summary, example} для builtin'а;
    не найден — ok=false, kind=not_found, hint builtin:<name> + did-you-mean.
    Пример: pgg_docs(symbol="clip"), pgg_docs(symbol="make_roof").
    """
    return _call("docs", {"symbol": symbol})


@mcp.tool()
def pgg_views() -> dict:
    """Именованные виды загруженного файла (<stem>.views.json).

    Ответ data: {file, views:[{name, node, target?, orbit?, zoom?, ...}]}.
    Пример: pgg_views() после pgg_load(path="resources/pgg/spire_house.pgg").
    """
    return _call("views")


@mcp.tool()
def pgg_measure(node: str, a: str, b: str, axis: str = "x") -> dict:
    """Зазор между двумя bbox по оси — сахар над pgg_probe gap.

    a/b — ``group:<name>`` или голое имя группы на geo узла node.
    axis — x|y|z; перекрытие отрицательное. Пример:
    pgg_measure(node="house", a="group:stone", b="group:brick", axis="x").
    """
    spec = f"{node}:gap[a={a}, b={b}, axis={axis}]"
    return _call("probe", {"spec": spec})


@mcp.tool()
def pgg_contact_sheet(views: Any = "*", cols: Optional[int] = None,
                      size: Optional[list[float]] = None) -> dict:
    """Сетка именованных видов: цикл pgg_render + склейка Pillow.

    views="*" — все виды из .views.json; иначе список имён или строка через запятую.
    Подписи — имя вида. PNG → tmp/pgg_rpc_shots/contact_sheet.png.
    """
    try:
        from PIL import Image, ImageDraw
    except ImportError:
        return {"ok": False, "error": {"kind": "missing_dep",
                                       "message": "Pillow is required (tools/pgg_mcp/requirements.txt)"}}

    listed = _call("views")
    if not listed.get("ok"):
        return listed
    available = listed.get("data", {}).get("views") or []
    by_name = {v.get("name"): v for v in available if v.get("name")}
    if views == "*" or views is None:
        names = [v.get("name") for v in available if v.get("name")]
    elif isinstance(views, str):
        names = [s.strip() for s in views.split(",") if s.strip()]
    else:
        names = [str(s) for s in views]
    missing = [n for n in names if n not in by_name]
    if missing:
        return {"ok": False, "error": {"kind": "bad_args",
                                       "message": "unknown views: " + ", ".join(missing)}}
    if not names:
        return {"ok": False, "error": {"kind": "bad_args", "message": "no named views loaded"}}

    shots: list[tuple[str, Path]] = []
    stats: list[Any] = []
    for name in names:
        resp = pgg_render(view=name, png=True, size=size, chrome="off")
        if not resp.get("ok"):
            return resp
        data = resp.get("data") or {}
        path = data.get("path")
        if not path:
            return {"ok": False, "error": {"kind": "io_error",
                                           "message": f"view '{name}' returned no PNG path"}}
        shots.append((name, Path(path)))
        stats.append({"name": name, "stats": data.get("stats"),
                      "camera": data.get("camera"), "render_state": data.get("render_state")})

    images = []
    for name, path in shots:
        with Image.open(path) as im:
            images.append((name, im.convert("RGB")))
    cell_w = max(im.width for _, im in images)
    cell_h = max(im.height for _, im in images)
    caption = 22
    n = len(images)
    grid_c = cols if cols and cols > 0 else max(1, int(n ** 0.5 + 0.99))
    grid_r = (n + grid_c - 1) // grid_c
    sheet = Image.new("RGB", (grid_c * cell_w, grid_r * (cell_h + caption)), (18, 18, 20))
    draw = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(images):
        r, c = divmod(i, grid_c)
        x, y = c * cell_w, r * (cell_h + caption)
        sheet.paste(im, (x + (cell_w - im.width) // 2, y + caption + (cell_h - im.height) // 2))
        draw.text((x + 8, y + 3), name, fill=(230, 230, 230))
    out_dir = Path(_repo_root()) / "tmp" / "pgg_rpc_shots"
    out_dir.mkdir(parents=True, exist_ok=True)
    out_path = out_dir / "contact_sheet.png"
    sheet.save(out_path)
    return {"ok": True, "data": {"path": str(out_path), "width": sheet.width, "height": sheet.height,
                                 "views": names, "cells": stats}}


if __name__ == "__main__":
    mcp.run()
