# AGENTS.md

Инструкции для AI-агентов и людей, работающих с репозиторием Neverwhere.
Этот файл — **единый источник правды** по тому, «как здесь работать».
Подробное описание проекта см. в `README.md`, архитектурные детали — в `docs/`.

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
- `src/libs/highground_core` — генерация геометрии поднятой 3D-земли (стены + верх) по vertex-нодам; чистый конвейер `Grid + Params -> generate() -> Mesh`, без Qt/GPU. Общая для редактора/клиента (через `render_core`) и TileShapePlayground — отлаживать алгоритм удобнее в плейграунде.
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
- **RttrPlayground на macOS не собирается** (vcpkg-порт rttr не поддерживает osx; его CMakeLists возвращается сразу на не-Windows).

Платформенные особенности порта:

- **Рендер-бэкенды:** редактор и EcsPlayground — принудительный `SOKOL_GLCORE` (Qt OpenGL контекст), standalone-приложения — `SOKOL_METAL` (выбор в `render_core/sokol_config.h` и main.cpp по `__APPLE__`). Для GLCORE на macOS обязателен core profile: `QSurfaceFormat` 4.1 Core до создания `QApplication` (см. `main.cpp` редактора), иначе Qt даёт legacy 2.1 контекст и sokol падает на `sg_setup()` в Debug.
- **Шейдеры:** у `render_core` и playground-рендереров три варианта исходников — GLSL / HLSL / MSL; Metal-ветка выбирается по `sg_query_backend() == SG_BACKEND_METAL_MACOS` (runtime) или `#elif defined(SOKOL_METAL)` (compile-time). Единого `SG_BACKEND_METAL` в sokol нет.
- **sokol_app на macOS** требует Objective-C++: `main.cpp` standalone-приложений компилируется как ObjC++ через макрос `nw_configure_sokol_app(target)` в `cmake/utils.cmake` (там же линковка `Cocoa/QuartzCore/Metal/MetalKit`; на Windows — `d3d11/dxgi`). Все новые standalone sokol-приложения — через этот макрос.
- **glad нельзя включать в TU с `SOKOL_IMPL` на macOS** (и вообще вместе с Qt GL-заголовками): sokol_gfx/qopengl тянут системный `<OpenGL/gl3.h>`, который конфликтует с glad-макросами. GLCORE-бэкенд линкуется `-framework OpenGL`.
- **Ресурсы:** `baseDataPath` в `core_context.cpp` ищется вверх от cwd (repo root, `_intermediate_64` — оба сработают); хардкода абсолютных путей нет.
- **Код-подпись:** ad-hoc (`CODE_SIGN_IDENTITY "-"` в `nw_add_app_sources`, post-build `codesign -f -s -` у EpicMapEditor) — пустая identity в Xcode невалидна для CLI-таргетов.
- После обновления Xcode **сбрасывать CMake-кэш** (`cmake -U LIBRESOLV` или чистый `_intermediate_64`): `find_library` кэширует пути внутрь старого SDK.

### vcpkg и зависимости

- vcpkg подключён как submodule (`toolchain/vcpkg`). **Не патчить** `toolchain/vcpkg` напрямую.
- Зависимости — через `vcpkg.json`.
- Для overlay-фиксов — `vcpkg_overlays/ports`; путь уже подключён в `CMakePresets.json` через `VCPKG_OVERLAY_PORTS`.
  - Например, Sokol подключён через overlay, чтобы держать `util/sokol_imgui.h` совместимым с актуальным Dear ImGui из vcpkg.
- При добавлении зависимости: обновить `vcpkg.json` → `find_package(...)` → прилинковать импортированный target в `LIBS` у соответствующего `nw_add_...`.

### Индексация для IDE/clangd (Serena MCP, clangd LSP)

Основной флоу — Visual Studio generator, который **не** умеет эмиттить `compile_commands.json` (это ограничение CMake, не баг). Для Serena/clangd есть отдельный Ninja-preset, который только конфигурирует (не собирает).

- **Preset:** `ide-index` в `CMakePresets.json` — Ninja generator, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, отдельный `binaryDir = _intermediate_ide`.
- **Обёртка:** `generate_compile_commands.bat` — вызывает `vcvars64.bat` (Ninja не находит MSVC сам), конфигурит, копирует/симлинкает `compile_commands.json` в корень репо.
- **Запуск:** `generate_compile_commands.bat` (или `--clean` для полного ребилда кэша).
- **Когда перегенерировать:** после добавления/удаления исходников, изменения compile-флагов или `vcpkg.json`. После правок в существующих файлах — **не нужно** (clangd читает флаги из `compile_commands.json`, а не из самих исходников).
- `_intermediate_ide/` и `compile_commands.json` в `.gitignore` — это локальные artefacts.

Без `compile_commands.json` Serena работает на структурном parse (видит имена, но не типы), и `get_diagnostics_for_file` выдаёт ложный шум (`'QObject' file not found`, `Unknown type name 'Q_OBJECT'`). С ним — полноценная индексация: чистая диагностика, полные references, type-aware навигация.

## Undo/Redo

- Предпочитать immutable-снэпшоты через `immer`; избегать Command Pattern, если явно не просят. (Сейчас immer — `[концепт]`, см. `docs/TECHNICAL_STACK.md`.)

## Тестирование и прототипы

- Unit-тесты (gtest) — `src/tests/<domain>/<name>_test.cpp`, подхватываются GLOB'ом автоматически; бинарь `neverwhere_tests`.
  - Сборка: `cmake --build --preset debug --target neverwhere_tests`.
  - Запуск: `ctest --test-dir _intermediate_64 -C Debug --output-on-failure` или напрямую `_intermediate_64/Debug/neverwhere_tests.exe`.
- Новые тестовые приложения и прототипы — под `src/apps/<PrototypeName>`.
- Таргеты прототипов/тестов **обязаны** иметь суффикс `Playground` (напр. `RttrPlayground`, `EcsPlayground`).
- GUI-прототипы — `nw_add_qml_app`, CLI-прототипы — `nw_add_console_app`.
- Для smoke-тестов как агентской проверки — добавлять метод `runTestScenario()` в главную модель/логику; сценарий гоняет ключевые операции программно, без UI.
- Логировать результаты через `spdlog::info`/`spdlog::error` с маркерами `TEST PASS` / `TEST FAIL`.
- Если smoke-сценарий должен запускаться из приложения — триггерить через `QTimer::singleShot(1000, ...)` в `main.cpp`.

## Инструменты

- Для документации по библиотекам/API, codegen, setup — использовать Context7 MCP, не дожидаясь явного напоминания.

### Отладка падений через debug-MCP (`neverwhere-debug` / `neverwhere-debug-macos`)

MCP-сервер из `tools/debug_mcp/` — единый `debug_*` контракт, backend по платформе: **cdb** на Windows (`neverwhere-debug`, лаунчер `tools/run_mcp_server.ps1`) и **LLDB** на macOS (`neverwhere-debug-macos`, лаунчер `tools/run_mcp_server.sh`). Подключены в `.mcp.json`, запускаются автоматически вместе с IDE; нерабочая на данной ОС запись просто не стартует. Зависимости: `mcp>=1.0` (см. `tools/debug_mcp/requirements.txt`); на macOS — LLDB из Xcode (`xcrun lldb`), venv создаётся лаунчером при первом запуске.

**Принцип debugger-first (cdb-first):** при падении/ассерте/неверном рантайм-значении — сначала **доказательство через MCP** (`debug_*`), не фикс по стектрейсу из лога.

**Базовый flow для краша EpicMapEditor:**
1. `debug_self_check` — проверить, что отладчик найден, exe и символы на месте.
2. `debug_session_start(exe_target="EpicMapEditor")` — запустить inferior (или `debug_process_find(name_query="EpicMapEditor")` → `debug_session_start(process_id=<pid>)` для attach к уже бегущему).
3. `debug_run_until_stop` — гонять до падения/брейкпоинта.
4. `debug_stack_get` / `debug_scopes_get` / `debug_expression_eval` — стек, локали, выражения в точке сбоя. Реальная точка — **первый проектный фрейм** (под `repo_root()`), не верхний CRT/SEH-трамплин.
5. `debug_session_stop` — закрыть сессию.

**Разрешение exe-таргета:** Windows — `_intermediate_64/<config>/<target>.exe`; macOS — `_intermediate_64/src/apps/<target>/<config>/<target>` (по умолчанию `EpicMapEditor`, конфиг `Debug`). Override: env `NEVERWHERE_DEBUG_EXE` / `NEVERWHERE_DEBUG_EXE_TARGET` / `NEVERWHERE_DEBUG_EXE_CONFIG`, или пин через `debug_set_active(cmake_target="...")` → `.zcode/debug_active.json`.

**cdb search order (win):** env `CRASH_ANALYSIS_DEBUGGER_PATH` → Windows Kits (`C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe`). В `.mcp.json` путь уже прописан.

**macOS-особенности:** SIGSEGV/SIGABRT приходят как Mach-exception (`EXC_BAD_ACCESS`) — смотри `stop_event.exception.signal_equivalent`; attach только к своим процессам (SIP); DWARF в бинаре, отдельных символов не нужно; smoke backend'а без MCP — `tools/debug_mcp/smoke_lldb_worker.py`. LLDB backend живёт в `lldb_backend.py` + `lldb_worker.py` (worker на системном python3 3.9, т.к. SB API собран под cp39). Дополнительно: `debug_crash_report` — полный слепок при останове за один вызов (стеки всех потоков, project_frame + scopes + `this`, регистры, хвост лога inferior), `debug_variable_expand` — дрилл-даун по вложенным структурам (`this->layers`, `(*this).map`); на Windows оба пока `not_supported`.

**Post-mortem (.dmp):** `debug_crash_dump_analyze(dump_path)` — анализ готового дампа (Windows). Сейчас в neverwhere нет C++ хука для записи дампов при падении (это отдельный следующий шаг: `MiniDumpWriteDump` в `main.cpp` + `CRASH_DUMP_DIR`).

Полный список инструментов и контракт — `debug_contract_get`. Детальная методичка (flow, gotchas, first-chance exceptions, чек-лист) — `docs/debugging.md`.

### Автоматизация редактора (editor RPC + MCP)

EpicMapEditor поднимает TCP RPC-сервер на `127.0.0.1:9877` (построчный JSON, команды на GUI-потоке) — `src/apps/EpicMapEditor/src/editor_rpc_server.cpp`. Редактор должен быть запущен.

Операции:

- **Сессия:** `ping`, `status`, `list_chapters`, `load_chapter`, `create_chapter`, `save`, `reload`, `play`.
- **UI-стиль:** `list_assets`, `select_asset`, `select_tool`, `click` (экранные пиксели, зависит от камеры/зума).
- **Авторинг в координатах клеток** (идемпотентно, не зависит от камеры): `set_tile` / `erase_tile` / `fill_rect` (image-ассеты), `set_landscape` (bulk-апдейты нод `[[x,y,0|1],...]` на slice-ассете), чтение — `get_map` (опц. `layer`), визуальная проверка — `set_camera` + `screenshot` (PNG).

Имена слоёв для оп: `Decoration` | `BaseLandscape` | `GameplayInteractive` | `RaisedLandscape` (enum `LayerTypes`). Запись идёт через `MapAuthoring` (`src/libs/core/map/map_authoring.h`), он же покрыт gtest (`src/tests/map/map_authoring_test.cpp`). `set_landscape` целится в слой по `layerType` ассета: slice-ассеты → `BaseLandscape`, shape3d-ассеты (поднятая 3D-земля) → `RaisedLandscape`.

MCP-обёртка: серверы `neverwhere-editor` (macOS) / `neverwhere-editor-win` в `.mcp.json`, код — `tools/editor_mcp/` (тонкий прокси, инструменты `editor_*`; TCP-клиент переиспользован из `tools/debug_mcp/editor_rpc_client.py`, пригоден и для ручных скриптов). Типовой цикл «карта по описанию»: `create_chapter`/`load_chapter` → `list_assets` → `set_landscape` → `fill_rect`/`set_tile` → `set_camera` + `screenshot` → `get_map` → `save` → `play`.

## Где что искать

- `README.md` — что это за проект, высокоуровневая архитектура.
- `docs/ROADMAP_VISION.md` — продуктовое видение и статусы фич.
- `docs/TECHNICAL_STACK.md` — стек и решения (что есть / прототип / концепт).
- `docs/reference/` — бумаги и порты по процедурному рельефу.
- `docs/debugging.md` — отладка через cdb-MCP: flow, gotchas, first-chance exceptions, чек-лист.
