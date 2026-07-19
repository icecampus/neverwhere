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
- Бинарные директории: `_intermediate_64` (Windows), `_b-em` (Emscripten).
- **Не использовать** `build.sh` для Windows-флоу.

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

### Отладка падений через cdb-MCP (`neverwhere-debug`)

MCP-сервер `neverwhere-debug` (`tools/debug_mcp/`, запуск через `tools/run_mcp_server.ps1`) — обёртка над **cdb** (Windows native C++ debugger). Подключён в `.mcp.json`, запускается автоматически вместе с IDE. Зависимости: `mcp>=1.0` (см. `tools/debug_mcp/requirements.txt`), cdb из Windows SDK.

**Принцип cdb-first:** при падении/ассерте/неверном рантайм-значении — сначала **доказательство через MCP** (`debug_*`), не фикс по стектрейсу из лога.

**Базовый flow для краша EpicMapEditor:**
1. `debug_self_check` — проверить, что cdb найден, EpicMapEditor.exe и .pdb на месте.
2. `debug_session_start(exe_target="EpicMapEditor")` — запустить inferior под cdb (или `debug_process_find(name_query="EpicMapEditor")` → `debug_session_start(process_id=<pid>)` для attach к уже бегущему).
3. `debug_run_until_stop` — гонять до падения/брейкпоинта.
4. `debug_stack_get` / `debug_scopes_get` / `debug_expression_eval` — стек, локали, выражения в точке сбоя. Реальная точка — **первый проектный фрейм** (под `repo_root()`), не верхний CRT/SEH-трамплин.
5. `debug_session_stop` — закрыть сессию.

**Разрешение exe-таргета:** `_intermediate_64/<config>/<target>.exe` (по умолчанию `EpicMapEditor`, конфиг `Debug`). Override: env `NEVERWHERE_DEBUG_EXE` / `NEVERWHERE_DEBUG_EXE_TARGET` / `NEVERWHERE_DEBUG_EXE_CONFIG`, или пин через `debug_set_active(cmake_target="...")` → `.zcode/debug_active.json`.

**cdb search order:** env `CRASH_ANALYSIS_DEBUGGER_PATH` → Windows Kits (`C:\Program Files (x86)\Windows Kits\10\Debuggers\x64\cdb.exe`). В `.mcp.json` путь уже прописан.

**Post-mortem (.dmp):** `debug_crash_dump_analyze(dump_path)` — анализ готового дампа. Сейчас в neverwhere нет C++ хука для записи минидампов при SEH (это отдельный следующий шаг: `MiniDumpWriteDump` в `main.cpp` + `CRASH_DUMP_DIR`).

Полный список инструментов и контракт — `debug_contract_get`. Детальная методичка (flow, gotchas, first-chance exceptions, moc-кэш, чек-лист) — `docs/debugging.md`. Код бэкенда — `tools/debug_mcp/debugger_mcp.py`.

## Где что искать

- `README.md` — что это за проект, высокоуровневая архитектура.
- `docs/ROADMAP_VISION.md` — продуктовое видение и статусы фич.
- `docs/TECHNICAL_STACK.md` — стек и решения (что есть / прототип / концепт).
- `docs/reference/` — бумаги и порты по процедурному рельефу.
- `docs/debugging.md` — отладка через cdb-MCP: flow, gotchas, first-chance exceptions, чек-лист.
