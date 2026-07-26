#!/usr/bin/env python3
"""Fetch a Shadertoy shader into docs/reference/shadertoy/<ID>_<slug>/.

shadertoy.com sits behind a Cloudflare JS challenge, so plain curl/headless
requests get 403. This script drives the system Google Chrome via Playwright
(real browser passes the challenge): it opens the site's own public JSON API
(https://www.shadertoy.com/api/v1/shaders/<ID>?key=Bt8MHH — the key the
shadertoy frontend itself uses), waits for the challenge to clear, then saves:

    docs/reference/shadertoy/<ID>_<slug>/
        meta.json        id, name, author, description, tags, link, inputs
        Image.glsl       one file per render pass (BufferA.glsl, Common.glsl...)

and adds a row to the index in docs/reference/shadertoy/README.md.

Usage:
    fetch_shader.py <url-or-id> [--note "зачем взят"] [--headed]

--headed forces a visible browser window (fallback if headless gets stuck on
the Cloudflare page; the window closes itself once the shader is fetched).
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

API_URL = "https://www.shadertoy.com/api/v1/shaders/{id}?key=Bt8MHH"
VIEW_URL = "https://www.shadertoy.com/view/{id}"

REPO_ROOT = Path(__file__).resolve().parents[2]
OUT_ROOT = REPO_ROOT / "docs" / "reference" / "shadertoy"
INDEX_MD = OUT_ROOT / "README.md"
# Dedicated persistent Chrome profile: after the Cloudflare check is passed
# once (manual checkbox click in a --headed run), cf_clearance lives here and
# later fetches (incl. headless) go through without interaction.
CHROME_PROFILE = Path(__file__).resolve().parent / ".chrome-profile"

CHALLENGE_MARKERS = ("Just a moment", "Attention Required", "challenges.cloudflare.com")


def shader_id_from(text: str) -> str:
    text = text.strip()
    m = re.search(r"/(?:view|shaders)/([A-Za-z0-9]{6})(?:\D|$)", text)
    if not m:
        m = re.fullmatch(r"([A-Za-z0-9]{6})", text)
    if not m:
        raise SystemExit(f"не похоже на Shadertoy URL/ID: {text!r}")
    return m.group(1)


def slugify(name: str) -> str:
    slug = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-")
    return slug or "shader"


def sanitize_dirname(name: str) -> str:
    """Имя папки = тайтл демки; чистим только недопустимые для ФС символы."""
    name = re.sub(r'[\\/:*?"<>|]', "", name)
    return re.sub(r"\s+", " ", name).strip() or "shader"


def fetch_via_archive(shader_id: str, timeout_s: float = 60.0) -> dict | None:
    """web.archive.org fallback: shadertoy's API responses are often archived
    (the archive is NOT behind Cloudflare, unlike shadertoy.com itself).
    Returns {"shader": <api json>, "snapshot": <yyyymmddhhmmss>} or None."""
    import urllib.parse
    import urllib.request

    cdx = (
        "https://web.archive.org/cdx/search/cdx?output=json&limit=-5"
        "&filter=statuscode:200&filter=mimetype:application/json&url="
        + urllib.parse.quote(f"shadertoy.com/api/v1/shaders/{shader_id}*", safe="")
    )
    try:
        with urllib.request.urlopen(cdx, timeout=timeout_s) as r:
            rows = json.load(r)
    except Exception as e:
        print(f"[archive] CDX lookup failed: {e}")
        return None
    if len(rows) < 2:
        print("[archive] нет архивных снапшотов API-ответа")
        return None

    for row in reversed(rows[1:]):  # свежие сначала
        ts, original = row[1], row[2]
        url = f"https://web.archive.org/web/{ts}id_/{original}"
        try:
            with urllib.request.urlopen(url, timeout=timeout_s) as r:
                data = json.load(r)
        except Exception as e:
            print(f"[archive] snapshot {ts}: {e}")
            continue
        if isinstance(data, dict) and "Shader" in data:
            print(f"[archive] snapshot {ts}")
            return {"shader": data, "snapshot": ts}
    return None


def fetch_api_json(shader_id: str, headed: bool, timeout_s: float = 60.0) -> dict:
    """Open the view page and capture the shader JSON from the site's own
    /api/v1/shaders/ request (the frontend's public API key rotates, so we
    steal the live URL from network traffic instead of hardcoding a key)."""
    from playwright.sync_api import sync_playwright

    captured: dict = {}

    with sync_playwright() as p:
        context = p.chromium.launch_persistent_context(
            user_data_dir=str(CHROME_PROFILE),
            channel="chrome",
            headless=not headed,
            viewport={"width": 1280, "height": 800},
            locale="en-US",
            # anti-detection: Cloudflare loops the managed challenge on
            # automation-flagged Chrome (navigator.webdriver, --enable-automation)
            ignore_default_args=["--enable-automation"],
            args=[
                "--disable-gpu",
                "--enable-unsafe-swiftshader",
                "--disable-blink-features=AutomationControlled",
            ],
        )
        context.add_init_script(
            "Object.defineProperty(navigator, 'webdriver', {get: () => undefined});"
        )
        page = context.pages[0] if context.pages else context.new_page()

        def on_response(resp):
            if "/api/v1/shaders/" in resp.url and "key=" in resp.url and not captured:
                try:
                    data = resp.json()
                except Exception:
                    return
                if isinstance(data, dict) and "Shader" in data:
                    captured.update(data)

        page.on("response", on_response)
        page.goto(VIEW_URL.format(id=shader_id), wait_until="domcontentloaded")

        if headed:
            print("[wait] если Cloudflare просит подтверждение — пройди его в окне; скрипт продолжит сам", flush=True)
        deadline = time.monotonic() + timeout_s
        while not captured and time.monotonic() < deadline:
            if page.is_closed():
                raise RuntimeError("вкладка закрылась (вероятно GPU-краш на WebGL-инициализации)")
            try:
                page.wait_for_timeout(1000)
            except Exception as e:
                raise RuntimeError(f"страница умерла во время ожидания: {e}") from e
            print(f"[wait] {page.title()!r}", flush=True)
        context.close()

    if not captured:
        raise RuntimeError(
            f"не поймал shader JSON за {timeout_s:.0f}s — запусти с --headed и "
            "пройди проверку Cloudflare вручную (один раз, кука сохранится в профиле)"
        )
    return captured


def fetch_texture_files(renderpass: list[dict], folder: Path, timeout_s: float = 60.0) -> list[tuple[int, str]]:
    """Download texture inputs (iChannel*) into <folder>/textures/ via web.archive.org.
    Returns list of (channel, filename) actually saved."""
    import urllib.parse
    import urllib.request

    has_textures = any(
        inp.get("ctype") == "texture" and inp.get("src")
        for pass_ in renderpass
        for inp in pass_.get("inputs", [])
    )
    if not has_textures:
        return []

    saved: list[tuple[int, str]] = []

    tex_dir = folder / "textures"
    tex_dir.mkdir(exist_ok=True)
    used: set[str] = set()
    for pass_ in renderpass:
        for inp in pass_.get("inputs", []):
            if inp.get("ctype") != "texture" or not inp.get("src"):
                continue
            src = inp["src"]
            full_url = urllib.parse.urljoin("https://www.shadertoy.com/", src)
            suffix = Path(src).suffix or ".jpg"
            fname = f"iChannel{inp.get('channel', 0)}{suffix}"
            if fname in used:  # коллизия канала — fallback на имя медиафайла
                fname = Path(src).name
            used.add(fname)
            # SURT-ключи archive.org не содержат схему и www
            cdx_url = full_url.removeprefix("https://").removeprefix("http://").removeprefix("www.")
            cdx = (
                "https://web.archive.org/cdx/search/cdx?output=json&limit=-3"
                "&filter=statuscode:200&url=" + urllib.parse.quote(cdx_url, safe="")
            )
            try:
                with urllib.request.urlopen(cdx, timeout=timeout_s) as r:
                    rows = json.load(r)
                if len(rows) < 2:
                    raise RuntimeError("нет снапшотов в архиве")
                ts = rows[-1][1]
                with urllib.request.urlopen(
                    f"https://web.archive.org/web/{ts}id_/{full_url}", timeout=timeout_s
                ) as r:
                    (tex_dir / fname).write_bytes(r.read())
                saved.append((inp.get("channel", 0), fname))
                print(f"[save] {folder.name}/textures/{fname} (archive {ts})")
            except Exception as e:
                print(f"[warn] текстура {fname}: {e}")
    return saved


def render_pass_filename(pass_: dict, used: set[str]) -> str:
    raw = pass_.get("name") or pass_.get("type") or "pass"
    # "Buf A" -> "BufferA", "Image" -> "Image", "Common" -> "Common"
    name = re.sub(r"\bbuf\b", "Buffer", raw, flags=re.IGNORECASE)
    name = re.sub(r"[^A-Za-z0-9]+", "", name)
    if not name:
        name = "Pass"
    candidate, i = name, 2
    while candidate in used:
        candidate, i = f"{name}{i}", i + 1
    used.add(candidate)
    return f"{candidate}.glsl"


def write_readme(
    folder: Path,
    shader_id: str,
    name: str,
    author: str,
    info: dict,
    source: str,
    passes: list[dict],
    textures: list[tuple[int, str]],
) -> None:
    tags = info.get("tags") or []
    lines = [
        f"# {name}",
        "",
        f"- Ссылка: {VIEW_URL.format(id=shader_id)}",
        f"- Автор: {author}",
    ]
    if tags:
        lines.append(f"- Теги: {', '.join(tags)}")
    lines.append(f"- Источник: {source}")
    desc = (info.get("description") or "").strip()
    if desc:
        lines += ["", desc]
    lines += ["", "## Файлы", ""]
    for p in passes:
        inputs = ", ".join(
            f"iChannel{inp.get('channel')}: {inp.get('ctype')}" for inp in p["inputs"]
        ) or "без входов"
        lines.append(f"- `{p['file']}` — пасс `{p['type']}` ({inputs}).")
    for ch, fname in textures:
        lines.append(f"- `textures/{fname}` — текстура iChannel{ch} (mipmap/repeat).")
    (folder / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


def update_index(shader_id: str, name: str, author: str, note: str, folder: str) -> None:
    row = f"| [{name}](<{folder}/README.md>) | {author} | [{shader_id}]({VIEW_URL.format(id=shader_id)}) | {note or '—'} |\n"
    text = INDEX_MD.read_text(encoding="utf-8")
    if f"]({VIEW_URL.format(id=shader_id)})" in text:
        return  # уже в индексе
    INDEX_MD.write_text(text.rstrip("\n") + "\n" + row, encoding="utf-8")


def make_stub(url_or_id: str, title: str, note: str, force: bool) -> int:
    """Только каркас: папка по тайтлу + README-заготовка + строка в индексе.
    Контент (шейдеры, текстуры) наполняется вручную."""
    shader_id = shader_id_from(url_or_id)
    folder = OUT_ROOT / sanitize_dirname(title)
    folder.mkdir(parents=True, exist_ok=True)
    readme = folder / "README.md"
    if readme.exists() and not force:
        print(f"[skip] {folder.name}/README.md уже существует")
    else:
        readme.write_text(
            f"# {title}\n\n- Ссылка: {VIEW_URL.format(id=shader_id)}\n- Автор: —\n\n"
            "## Файлы\n\n_(наполняется вручную)_\n",
            encoding="utf-8",
        )
        print(f"[stub] {folder.name}/README.md")
    update_index(shader_id, title, "—", note or "заготовка, наполняется вручную", folder.name)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="Fetch Shadertoy shader into docs/reference/shadertoy/")
    ap.add_argument("shader", help="Shadertoy URL (…/view/XtyGzc) или 6-символьный ID")
    ap.add_argument("--note", default="", help="короткая заметка 'зачем взят' для индекса")
    ap.add_argument("--headed", action="store_true", help="показать окно браузера (если headless не проходит Cloudflare)")
    ap.add_argument("--no-archive", action="store_true", help="не ходить в web.archive.org, сразу живой сайт")
    ap.add_argument("--force", action="store_true", help="перезаписать README.md, даже если он уже есть (затрёт ручные правки)")
    ap.add_argument("--stub", action="store_true", help="только создать папку + README-заготовку, без фетча (нужен --title)")
    ap.add_argument("--title", default="", help="тайтл демки (обязателен с --stub)")
    args = ap.parse_args()

    if args.stub:
        if not args.title:
            print("[fail] с --stub нужен --title", file=sys.stderr)
            return 1
        return make_stub(args.shader, args.title, args.note, args.force)

    shader_id = shader_id_from(args.shader)

    # 1) web.archive.org — без браузера и без Cloudflare (основной путь: с этой
    #    машины shadertoy.com закрыт CF-челленджем даже для реального Chrome).
    snapshot = None
    data = None
    if not args.no_archive:
        print(f"[fetch] {shader_id} via web.archive.org…")
        hit = fetch_via_archive(shader_id)
        if hit:
            data, snapshot = hit["shader"], hit["snapshot"]

    # 2) живой сайт через headed/headless Chrome (перехват API-ответа страницы)
    if data is None:
        print(f"[fetch] {shader_id} via {'headed' if args.headed else 'headless'} chrome…")
        try:
            # headed даёт время пройти проверку Cloudflare руками
            data = fetch_api_json(shader_id, headed=args.headed, timeout_s=180.0 if args.headed else 60.0)
        except Exception as e:
            print(f"[fail] {e}", file=sys.stderr)
            return 1

    if "Error" in data:
        print(f"[fail] API error: {data['Error']}", file=sys.stderr)
        return 1

    shader = data.get("Shader", {})
    info = shader.get("info", {})
    name = info.get("name") or shader_id
    author = info.get("username") or "?"
    source = ("web.archive.org snapshot " + snapshot) if snapshot else "shadertoy.com live"
    folder = OUT_ROOT / sanitize_dirname(name)
    folder.mkdir(parents=True, exist_ok=True)

    passes = []
    used_names: set[str] = set()
    for pass_ in shader.get("renderpass", []):
        fname = render_pass_filename(pass_, used_names)
        (folder / fname).write_text(pass_.get("code", ""), encoding="utf-8")
        passes.append({
            "file": fname,
            "type": pass_.get("type"),
            "inputs": pass_.get("inputs", []),
        })
        print(f"[save] {folder.name}/{fname} ({len(pass_.get('code', ''))} chars)")

    textures = fetch_texture_files(shader.get("renderpass", []), folder)

    # README может содержать ручные заметки (лицензии, особенности) — не затираем без --force
    readme = folder / "README.md"
    if readme.exists() and not args.force:
        print(f"[skip] {folder.name}/README.md уже существует (перезапись — с --force)")
    else:
        write_readme(folder, shader_id, name, author, info, source, passes, textures)

    update_index(shader_id, name, author, args.note, folder.name)
    print(f"[ok] {name} by {author} -> {folder.relative_to(REPO_ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
