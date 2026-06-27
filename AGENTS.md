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

## Undo/Redo

- Предпочитать immutable-снэпшоты через `immer`; избегать Command Pattern, если явно не просят. (Сейчас immer — `[концепт]`, см. `docs/TECHNICAL_STACK.md`.)

## Тестирование и прототипы

- Новые тестовые приложения и прототипы — под `src/apps/<PrototypeName>`.
- Таргеты прототипов/тестов **обязаны** иметь суффикс `Playground` (напр. `RttrPlayground`, `EcsPlayground`).
- GUI-прототипы — `nw_add_qml_app`, CLI-прототипы — `nw_add_console_app`.
- Для smoke-тестов как агентской проверки — добавлять метод `runTestScenario()` в главную модель/логику; сценарий гоняет ключевые операции программно, без UI.
- Логировать результаты через `spdlog::info`/`spdlog::error` с маркерами `TEST PASS` / `TEST FAIL`.
- Если smoke-сценарий должен запускаться из приложения — триггерить через `QTimer::singleShot(1000, ...)` в `main.cpp`.

## Инструменты

- Для документации по библиотекам/API, codegen, setup — использовать Context7 MCP, не дожидаясь явного напоминания.

## Где что искать

- `README.md` — что это за проект, высокоуровневая архитектура.
- `docs/ROADMAP_VISION.md` — продуктовое видение и статусы фич.
- `docs/TECHNICAL_STACK.md` — стек и решения (что есть / прототип / концепт).
- `docs/reference/` — бумаги и порты по процедурному рельефу.
