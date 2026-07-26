# Референсы с Shadertoy

Коллекция шейдеров-референсов для дальнейшей работы (порты в `render_core`, прототипы).
Один каталог на шейдер: `<ID>_<slug>/` с исходниками пассов (`Image.glsl`, `BufferA.glsl`,
`Common.glsl`, …) и `meta.json` (название, автор, описание, теги, инпуты, ссылка).

## Как добавлять

Агентом: `tools/shadertoy/.venv/bin/python tools/shadertoy/fetch_shader.py <url-или-ID> [--note "зачем"]`.
Скрипт сам раскладывает по структуре и дописывает строку в индекс ниже.

Маршруты фетча (shadertoy.com с этой машины закрыт Cloudflare-челленджем намертво —
curl, headless и даже headed Chrome крутят «Just a moment…» бесконечно):

1. **web.archive.org** (основной, без браузера): API-ответы Shadertoy часто заархивированы,
   архив за Cloudflare не стоит. Скрипт ищет через CDX API свежайший снапшот
   `shadertoy.com/api/v1/shaders/<ID>*` и забирает его. Минус: версия шейдера на дату снапшота.
2. **Живой сайт через Playwright + системный Chrome** (`--headed`): перехват API-ответа
   страницы шейдера из сетевого трафика (публичный API-ключ фронтенда ротируется, поэтому
   ключ не захардкожен). Сейчас бесполезен против CF — оставлен на случай смены IP/VPN.
3. **Руками**: открыть `https://www.shadertoy.com/view/<ID>` в браузере, который проходит
   CF, скопировать код пассов и отдать агенту — он разложит по структуре.

Зависимости скрипта: venv `tools/shadertoy/.venv` (playwright; создаётся
`python3 -m venv tools/shadertoy/.venv && tools/shadertoy/.venv/bin/pip install playwright`),
persistent Chrome-профиль `tools/shadertoy/.chrome-profile` (cf_clearance, если CF когда-то отпустит).
Для архивного маршрута venv не нужен — хватает системного python3.

## Индекс

| ID | Название | Автор | Ссылка | Зачем взят |
|----|----------|-------|--------|------------|
| XtyGzc | Desert Passage | Shane | [link](https://www.shadertoy.com/view/XtyGzc) | raymarch-каньон: cellular/tile-структура породы, реф для cliff-генерации |
| MsXGzM | Voronoi - rocks | iq | [link](https://www.shadertoy.com/view/MsXGzM) | voronoi-структура скал + fbm из текстур, реф для rock/cliff-поверхности |
