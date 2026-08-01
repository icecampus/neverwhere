# Референсы с Shadertoy

Коллекция шейдеров-референсов для дальнейшей работы (порты в `render_core`, прототипы).

**Формат:** одна папка на демку, имя папки = тайтл демки. Внутри — `README.md` со ссылкой
на Shadertoy и описанием, файлы шейдеров (`Image.glsl`, `BufferA.glsl`, …) и прочие ресурсы
(`textures/` — текстуры iChannel, если удалось добыть).

Папки подхватывает **Shadertoy** (`src/landscape_playgrounds/Shadertoy`, см. AGENTS.md)
без перекомпиляции: `Image.glsl`/`BufferA..D.glsl`/`Common.glsl` — по именам,
текстуры `textures/iChannelN.(png|jpg)` — биндинг канала по имени файла.

## shadertoy.json (манифест для multi-pass)

Входы буферов (какой буфер в какой iChannel) на shadertoy.com — метаданные, в GLSL их
нет, поэтому для multi-pass демок в папку кладётся `shadertoy.json`:

```json
{
  "passes": {
    "BufferB": { "iChannel0": "BufferB" },
    "Image":    { "iChannel0": "BufferA", "iChannel2": "BufferB" }
  }
}
```

Значение — имя буфера (`BufferA`..`BufferD`). Чтение идёт через ping-pong: буфер видит
свой прошлый кадр (self-feedback), Image — текущий кадр буферов. Текстурные каналы
(файл `textures/iChannelN.*`) манифест не требуют и не переопределяются им; single-pass
демкам манифест не нужен вообще. Если точная текстура оригинала недоступна — подбираем
близкую (mipmap/repeat) и помечаем это в README демки.

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
| [Jagged Plain](<Jagged Plain/README.md>) | — | [4tSXRm](https://www.shadertoy.com/view/4tSXRm) | заготовка, наполняется вручную |
| [Desert Passage II](<Desert Passage II/README.md>) | — | [WdGcDt](https://www.shadertoy.com/view/WdGcDt) | заготовка, наполняется вручную |
| [Cave Entrance](<Cave Entrance/README.md>) | — | [ltjXzd](https://www.shadertoy.com/view/ltjXzd) | заготовка, наполняется вручную |
| [Canyon Pass](<Canyon Pass/README.md>) | Shane | [MlG3zh](https://www.shadertoy.com/view/MlG3zh) | классический каньон, DF + texture bump (база для Antelope Canyon) |
| [Rock Shapes WIP](<Rock Shapes WIP/README.md>) | — | [XsfXDl](https://www.shadertoy.com/view/XsfXDl) | заготовка, наполняется вручную |
| [Cavernic](<Cavernic/README.md>) | Leon Denise | [DlB3WV](https://www.shadertoy.com/view/DlB3WV) | noise-пещера с камнем и водой |
| [Rounded Voro Rocks](<Rounded Voro Rocks/README.md>) | Dain | [MdlcDS](https://www.shadertoy.com/view/MdlcDS) | rounded voro-скалы (по Shane Cellular Tile Tunnel) |
| [Green Hayduke](<Green Hayduke/README.md>) | — | [llcXRN](https://www.shadertoy.com/view/llcXRN) | заготовка, наполняется вручную |
| [Noise Layer Raymarch Traversal](<Noise Layer Raymarch Traversal/README.md>) | — | [3XSSzw](https://www.shadertoy.com/view/3XSSzw) | заготовка, наполняется вручную |
| [Between Worlds](<Between Worlds/README.md>) | — | [4scGWr](https://www.shadertoy.com/view/4scGWr) | твик Shane 'Cave Entrance' + атмосфера |
| [Rocky Tunnel 2](<Rocky Tunnel 2/README.md>) | — | [llGGWc](https://www.shadertoy.com/view/llGGWc) | заготовка, наполняется вручную |
| [Glowing Spiral Lava Tunnel](<Glowing Spiral Lava Tunnel/README.md>) | — | [Wcd3W2](https://www.shadertoy.com/view/Wcd3W2) | заготовка, наполняется вручную |
| [Antelope Canyon](<Antelope Canyon/README.md>) | — | [4tGfzz](https://www.shadertoy.com/view/4tGfzz) | форк Canyon Pass: каньон Антилопы, DF + texture bump |
| [Blackbody Flowing Lava](<Blackbody Flowing Lava/README.md>) | — | [ddVBR3](https://www.shadertoy.com/view/ddVBR3) | 3 пасса: perlin-лава с blackbody-цветом, камера мышью |
| [very noi](<very noi/README.md>) | — | [3t23z1](https://www.shadertoy.com/view/3t23z1) | заготовка, наполняется вручную |
| [Tumble Rock](<Tumble Rock/README.md>) | — | [NcjGDh](https://www.shadertoy.com/view/NcjGDh) | заготовка, наполняется вручную |
| [Flycam - Desert Passage by Shane](<Flycam - Desert Passage by Shane/README.md>) | — | [WsGfWm](https://www.shadertoy.com/view/WsGfWm) | заготовка, наполняется вручную |
| [Flowing Water Over The Rock](<Flowing Water Over The Rock/README.md>) | — | [mtKyzw](https://www.shadertoy.com/view/mtKyzw) | заготовка, наполняется вручную |
| [ProfilePicture 120003 (mouse L-R)](<ProfilePicture 120003 (mouse L-R)/README.md>) | — | [WcyyWG](https://www.shadertoy.com/view/WcyyWG) | заготовка, наполняется вручную |
| [Rocknot](<Rocknot/README.md>) | — | [XccXWX](https://www.shadertoy.com/view/XccXWX) | заготовка, наполняется вручную |
| [Code 33b Grassy Rocks](<Code 33b Grassy Rocks/README.md>) | — | [MXjcRc](https://www.shadertoy.com/view/MXjcRc) | заготовка, наполняется вручную |
