# AGENTS.md

Инструкции для AI-агентов и людей, работающих с репозиторием Neverwhere.
Этот файл — **единый источник правды** по тому, «как здесь работать».
Подробное описание проекта см. в `README.md`, архитектурные детали — в `docs/`.

Что где лежит подробно — в разделе «Где что искать» внизу. **Правило:** этот файл описывает, *как здесь работать*; устройство конкретной подсистемы и её грабли живут в `docs/…`, и правя код подсистемы, обновляй её документ, а не этот.

## Коммуникация и язык

- Общайся на русском, если явно не просят иначе.
- Идентификаторы в коде (комментарии, имена переменных, функций, классов) — на английском.
- Документацию пиши на том же языке, что и окружающий контекст (в этом репо преобладает русский).

## Репозиторий и архитектура

- Это **один monorepo**: приложения и библиотеки живут под существующей структурой (`src/apps/`, `src/libs/`).
- Не плодить изолированные проекты со своими `project(...)` без необходимости.
- **Основной стек:** C++20 для логики, Qt 6 / QML для UI редактора, Dear ImGui для UI рантайма.
- Не вводить тяжёлые зависимости без согласования.
- `src/libs/core` — бизнес-логика ядра; `src/apps/EpicMapEditor` — UI/приложение редактора.
- Ландшафтные генераторы — no-Qt/no-GPU либы по паттерну `Grid + Params -> generate() -> Mesh`: `src/libs/highground_core` (поднятая земля, cliff/tech/mask-поля, surface nets), `src/libs/landscape_mesh` (клеточная маска → plateau-полоса, стили стен), `src/libs/stone_gen` (voronoi-камни, бейк), `src/libs/fence_core` (модель забора + инстансинг печёных мешей). Рендер, общий для редактора и клиента, — `src/libs/render_core`.
- Типы ландшафтных ассетов редактора (`shape3d`/`cliff3d`/`stone3d`/`cyclopean3d`/`tech3d`/`mask3d`/`texture2d`/`fence3d`), шаблон интеграции нового типа (данные → слой → рендерер → кисть → RPC → тесты → бандл) и их грабли — `docs/landscape_assets.md`. Безатласным типам обязателен `thumbnail` в `index.json`, иначе ячейка палитры пустая.
- Единая модель глубины (уровень фрагмента = мировая высота; вода/сетка = 0, верх хайграунда = +1, подводное < 0), порядок мировых проходов и sokol-грабли (альфа в бленде, depth-сэмплинг, юниформы) — `docs/render_depth_model.md`. Все новые проходы пекут z по формуле из `render_core/depth_levels.h`.
- Язык процедурной генерации геометрии PGG: спецификация — `docs/geometry_generation_language.md`, реализация (`src/libs/pgg`, PggTool, PggViewer, корпус, грабли) — `docs/pgg_implementation.md`.
- Плейграунды и их хроники — `docs/playgrounds/README.md`.
- QML и C++-логику держать разделёнными; связывать через Qt properties/signals/slots.
- Чистые сериализуемые данные — в `game_data` (без Qt); `QObject`-обёртки — только для view-логики в редакторе.
- Перед архитектурными или продуктовыми решениями сверяться с `docs/ROADMAP_VISION.md` и `docs/TECHNICAL_STACK.md`.

## CMake и макросы

- Проектные CMake-скрипты и макросы — в `cmake/utils.cmake`.
- Использовать общие макросы: `nw_add_qml_app(...)`, `nw_add_console_app(...)`, `nw_add_lib_sources(...)` и др. `nw_add_...`.
- QML-приложения — через `nw_add_qml_app(NAME ... LIBS ...)`; CLI-прототипы — через `nw_add_console_app`.
- Зависимости передавать через `LIBS`. У макросов общий include path на `src/libs`, подключается `pch.h`, на MSVC делается post-build деплой через `windeployqt`.
- В каждой lib/app есть `pch.h` — подключать первым.

## Сборка (Windows, CMake + vcpkg)

Основной Windows-флоу — Visual Studio 2022 + CMake Presets.

- Сгенерировать solution: `generate_vs.bat` (при необходимости бутстрапит vcpkg).
  - Solution: `_intermediate_64\Neverwhere.sln`.
  - Без обёртки: `cmake --preset vs2022`.
- Сборка из CLI: `cmake --build --preset debug --target EpicMapEditor`.
- Бинарные директории: `_intermediate_64` (Windows и macOS), `_b-em` (Emscripten).
- **Не использовать** `build.sh` для Windows-флоу.

## Сборка (macOS, CMake + vcpkg)

macOS-флоу — Xcode generator + CMake Presets, та же `_intermediate_64`, триплет `arm64-osx`.

- Конфигурация: `./build_mac.sh` (обёртка над `cmake --preset macos`). Первая конфигурация собирает все vcpkg-зависимости (включая Qt) — занимает ~1–2 часа.
- Сборка из CLI: `cmake --build --preset macos-debug --target EpicMapEditor`.
- Бинарники: `_intermediate_64/src/apps/<App>/Debug/<App>` (command-line executables, не .app).
- Smoke-проверки: `EpicGameClient --smoke`, `EpicMapEditor --smoke`; юнит-тесты — `_intermediate_64/src/tests/Debug/neverwhere_tests`.
- Платформенные особенности порта (GLCORE vs Metal, ObjC++ для sokol_app, glad vs `SOKOL_IMPL`, code-sign, сброс кэша после Xcode, CLion-пресет, RttrPlayground не собирается) — `docs/BUILD.md`.

### vcpkg и зависимости

- vcpkg подключён как submodule (`toolchain/vcpkg`). **Не патчить** `toolchain/vcpkg` напрямую.
- Зависимости — через `vcpkg.json`.
- Для overlay-фиксов — `vcpkg_overlays/ports`; путь уже подключён в `CMakePresets.json` через `VCPKG_OVERLAY_PORTS`.
  - Например, Sokol подключён через overlay, чтобы держать `util/sokol_imgui.h` совместимым с актуальным Dear ImGui из vcpkg.
- При добавлении зависимости: обновить `vcpkg.json` → `find_package(...)` → прилинковать импортированный target в `LIBS` у соответствующего `nw_add_...`.
- Сетевой binary cache подключён во всех пресетах (`docs/VCPKG_CACHE.md`, `docs/BUILD.md`): для push нужен `export NEVERWHERE_VCPKG_CACHE_AUTH="Authorization: Basic <base64(user:pass)>"`, чтение работает и без него.
- Индексация для clangd/Serena (`compile_commands.json`, пресет `ide-index`, `generate_compile_commands.bat`) — `docs/BUILD.md`.

## Сборка (Linux, CMake + vcpkg)

Linux-флоу — Ninja (single-config) + CMake Presets, триплет `x64-linux`, бинарная директория `_int_linux` (**не** `_intermediate_64` — та под win/mac-кэш).

- Конфигурация: `./build_linux.sh` (обёртка над `cmake --preset linux`). Первая конфигурация собирает все vcpkg-зависимости (включая Qt) — ~1–2 часа.
- Сборка из CLI: `cmake --build --preset linux-debug --target EpicMapEditor`.
- Релизная сборка: пресет `linux-release` (`CMAKE_BUILD_TYPE=Release`, отдельная бинарная директория `_int_linux_release`, наследует `linux`): конфигурация `cmake --preset linux-release`, сборка `cmake --build --preset linux-release --target EpicMapEditor`.
- Бинарники: `_int_linux/src/apps/<App>/Debug/<App>` (app-макросы кладут exe в подпапку `$<CONFIG>`, как VS/Xcode — иначе на single-config Ninja exe коллидирует с директорией qml-модуля того же имени).
- Smoke-проверки и юнит-тесты — как на macOS: `EpicMapEditor --smoke`, `_int_linux/src/tests/neverwhere_tests` (ctest: `ctest --test-dir _int_linux --output-on-failure`).
- Системные пакеты (Ubuntu), особенности порта (GLCORE, X11/XWayland, `OpenGL::GL` в Qt-приложениях) и скриншоты по RPC под `xvfb-run` (под Wayland `grabWindow` отдаёт чёрное) — `docs/BUILD.md`.

## Undo/Redo

- Предпочитать immutable-снэпшоты через `immer`; избегать Command Pattern, если явно не просят. (Сейчас immer — `[концепт]`, см. `docs/TECHNICAL_STACK.md`.)

## Тестирование и прототипы

- Unit-тесты (gtest) — `src/tests/<domain>/<name>_test.cpp`, подхватываются GLOB'ом автоматически; бинарь `neverwhere_tests`.
  - Сборка: `cmake --build --preset debug --target neverwhere_tests`.
  - Запуск: `ctest --test-dir _intermediate_64 -C Debug --output-on-failure` или напрямую `_intermediate_64/Debug/neverwhere_tests.exe`.
- Новые тестовые приложения и прототипы — под `src/apps/<PrototypeName>`; ландшафтные плейграунды вынесены в отдельную группу `src/landscape_playgrounds/` (HighgroundWithEffects, PolygonalGeneratedLandscape, SDFGeneratedLandscape, SDFWithMaterialLandscape, Shadertoy, StoneCube, TextureBlendLandscape, FencePathPlayground, B-repGeneratedLandscape, StoneGenerator) — подключается `add_subdirectory(src/landscape_playgrounds)` в корневом CMakeLists, внутри тот же GLOB по подпапкам, таргет = имя директории приложения.
- Таргеты прототипов/тестов **обязаны** иметь суффикс `Playground` (напр. `RttrPlayground`, `EcsPlayground`) — кроме группы `src/landscape_playgrounds/`: там принадлежность видна по корневой папке, поэтому суффикс у приложений убран.
- GUI-прототипы — `nw_add_qml_app`, CLI-прототипы — `nw_add_console_app`.
- Для smoke-тестов как агентской проверки — добавлять метод `runTestScenario()` в главную модель/логику; сценарий гоняет ключевые операции программно, без UI.
- Логировать результаты через `spdlog::info`/`spdlog::error` с маркерами `TEST PASS` / `TEST FAIL`.
- Если smoke-сценарий должен запускаться из приложения — триггерить через `QTimer::singleShot(1000, ...)` в `main.cpp`.

## Инструменты

- Для документации по библиотекам/API, codegen, setup — использовать Context7 MCP, не дожидаясь явного напоминания.
- Все MCP-серверы проекта (`.mcp.json`) и их назначение — `docs/mcp_servers.md`.

### Отладка падений через debug-MCP (`neverwhere-debug` / `neverwhere-debug-macos`)

**Принцип debugger-first (cdb-first):** при падении/ассерте/неверном рантайм-значении — сначала **доказательство через MCP** (`debug_*`), не фикс по стектрейсу из лога.

**Базовый flow для краша EpicMapEditor:**
1. `debug_self_check` — проверить, что отладчик найден, exe и символы на месте.
2. `debug_session_start(exe_target="EpicMapEditor")` — запустить inferior (или `debug_process_find(name_query="EpicMapEditor")` → `debug_session_start(process_id=<pid>)` для attach к уже бегущему).
3. `debug_run_until_stop` — гонять до падения/брейкпоинта.
4. `debug_stack_get` / `debug_scopes_get` / `debug_expression_eval` — стек, локали, выражения в точке сбоя. Реальная точка — **первый проектный фрейм** (под `repo_root()`), не верхний CRT/SEH-трамплин.
5. `debug_session_stop` — закрыть сессию.

Разрешение exe-таргета, cdb search order, macOS-особенности (Mach-exceptions, `debug_crash_report`/`debug_variable_expand`), post-mortem и gotchas — `docs/debugging.md`; полный контракт — `debug_contract_get`.

### Автоматизация редактора (editor RPC + MCP)

EpicMapEditor поднимает TCP RPC-сервер на `127.0.0.1:9877` (построчный JSON, команды на GUI-потоке; `src/apps/EpicMapEditor/src/editor_rpc_server.cpp`), MCP-обёртка — `neverwhere-editor`/`neverwhere-editor-win` (`tools/editor_mcp/`, инструменты `editor_*`). Авторинг — в координатах клеток через `MapAuthoring`, идемпотентно и независимо от камеры. Типовой цикл «карта по описанию»: `create_chapter`/`load_chapter` → `list_assets` → `set_landscape` / `fence_stroke` → `fill_rect`/`set_tile` → `set_camera` + `screenshot` → `get_map` → `save` → `play`. Полный список операций, имена слоёв, соответствие типов ассетов слоям и грабли камеры/скриншотов — `docs/editor_rpc.md`.

### PGG (язык процедурной генерации геометрии)

- Спецификация языка — `docs/geometry_generation_language.md` (ТЗ: текст-first нодовый граф для LLM-агентов + нодовая проекция; этапы и критерии — §15, история — §19).
- Заметки по реализации (этапы E0–E8 по файлам, грабли ANTLR/ядра, PggTool/PggViewer CLI, корпус и сьюты тестов) — `docs/pgg_implementation.md`. **Правя `src/libs/pgg`, `src/apps/PggTool`, `src/apps/PggViewer` или корпус, обновляй его, а не этот файл.**
- Коротко: `src/libs/pgg` (desktop-only, ANTLR4 4.13.2; сгенерированный парсер коммитится в `parser_gen/`, после правок `grammar/Pgg.g4` — `tools/pgg/regen_parser.sh`), ядро исполнения `src/libs/pgg/src/eval/`, тесты `src/tests/pgg/*_test.cpp` + корпус `src/tests/pgg/corpus/`, CLI `PggTool` (`check`/`fmt`/`ast`/`run`/`docs`), вьювер `PggViewer` (нодовая проекция + превью геометрии, `--smoke`).

## Где что искать

- `README.md` — что это за проект, высокоуровневая архитектура.
- `docs/ROADMAP_VISION.md` — продуктовое видение и статусы фич; `docs/TECHNICAL_STACK.md` — стек и решения (что есть / прототип / концепт).
- `docs/BUILD.md` — платформенные особенности сборки, vcpkg/binary cache (`docs/VCPKG_CACHE.md`), индексация для clangd.
- `docs/landscape_assets.md` — типы ландшафтных ассетов редактора и генераторы геометрии; `docs/render_depth_model.md` — модель глубины и порядок проходов.
- `docs/playgrounds/README.md` — индекс плейграундов (SDF-стенды и стыковка с землёй, B-rep, StoneGenerator, StoneCube, FencePath, ShapeML, Shadertoy).
- `docs/geometry_generation_language.md` — спецификация языка PGG; `docs/pgg_implementation.md` — заметки по его реализации.
- `docs/debugging.md` — отладка через debug-MCP; `docs/editor_rpc.md` — RPC редактора; `docs/mcp_servers.md` — все MCP-серверы.
- `docs/SDF_TO_MESH_PLAYBOOK.md` — рецепт переноса shadertoy SDF-демки в меш-генератор.
- `docs/reference/` — бумаги и порты по процедурному рельефу.
