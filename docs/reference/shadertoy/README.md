# Референсы с Shadertoy

Коллекция шейдеров-референсов для дальнейшей работы (порты в `render_core`, прототипы).

**Формат:** одна папка на демку, имя папки = тайтл демки. Внутри — `README.md` со ссылкой
на Shadertoy и описанием, файлы шейдеров (`Image.glsl`, `BufferA.glsl`, …) и прочие ресурсы
(`textures/` — текстуры iChannel, если удалось добыть).

## Как добавлять

**Основной флоу — ручная паста:** shadertoy.com с текущего IP закрыт Cloudflare намертво
(curl, headless и даже headed Chrome крутят «Just a moment…» бесконечно). Поэтому:
кидаем агенту название шейдера + код пассов (просто скопировать вкладки подряд:
`Buffer A /*...*/ , image /*...*/`), агент разрезает по пассам, раскладывает по формату
и дописывает строку в индекс ниже.

Запасные маршруты:

1. **Автофетч через web.archive.org** — `tools/shadertoy/.venv/bin/python tools/shadertoy/fetch_shader.py <url-или-ID> [--note "зачем"]`.
   API-ответы Shadertoy часто заархивированы (архив за Cloudflare не стоит); скрипт берёт
   свежайший снапшот, качает GLSL пассов и текстуры iChannel, раскладывает по формату выше.
   Работает для популярных старых шейдеров; минус — версия на дату снапшота.
   Для этого маршрута хватает системного python3.
2. **Живой сайт через Playwright + системный Chrome** (`--headed`): перехват API-ответа
   страницы шейдера из сетевого трафика. Сейчас бесполезен против CF — оставлен на случай
   смены IP/VPN. Нужен venv `tools/shadertoy/.venv`
   (`python3 -m venv tools/shadertoy/.venv && tools/shadertoy/.venv/bin/pip install playwright`).

## Индекс

| Демка | Автор | Shadertoy | Зачем взят |
|-------|-------|-----------|------------|
| [Desert Passage](<Desert Passage/README.md>) | Shane | [XtyGzc](https://www.shadertoy.com/view/XtyGzc) | raymarch-каньон: cellular/tile-структура породы, реф для cliff-генерации |
| [Voronoi - rocks](<Voronoi - rocks/README.md>) | iq | [MsXGzM](https://www.shadertoy.com/view/MsXGzM) | voronoi-структура скал + fbm из текстур, реф для rock/cliff-поверхности |
| [Underground Passageway](<Underground Passageway/README.md>) | Shane | [XdcfDf](https://www.shadertoy.com/view/XdcfDf) | precalc высотной карты скал в буфере (splatter 7×7 cell check), маппинг на туннель |
