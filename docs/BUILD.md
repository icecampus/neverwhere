# Сборка: платформенные особенности, зависимости, индексация

_Вынесено из `AGENTS.md` (2026-09-06): корневой файл — компактная входная точка, подробности подсистем живут в `docs/`. Правя соответствующий код, обновляй **этот** документ._

Команды configure/build/test по платформам — в `AGENTS.md` → «Сборка». Здесь — то, что нужно реже: грабли портов, системные пакеты, binary cache, индексация для clangd.

## macOS: платформенные особенности порта

- **CLion:** пресет `macos-clion` (Ninja, `_int_clion`, Debug + compile_commands) — CLion не поддерживает Xcode-генератор основного пресета. `macos-index` (`_intermediate_ide`) оставлен под индексацию Serena/clangd, в IDE его не используем.
- **RttrPlayground на macOS не собирается** (vcpkg-порт rttr не поддерживает osx; его CMakeLists возвращается сразу на не-Windows).

- **Рендер-бэкенды:** редактор и EcsPlayground — принудительный `SOKOL_GLCORE` (Qt OpenGL контекст), standalone-приложения — `SOKOL_METAL` (выбор в `render_core/sokol_config.h` и main.cpp по `__APPLE__`). Для GLCORE на macOS обязателен core profile: `QSurfaceFormat` 4.1 Core до создания `QApplication` (см. `main.cpp` редактора), иначе Qt даёт legacy 2.1 контекст и sokol падает на `sg_setup()` в Debug.
- **Шейдеры:** у `render_core` и playground-рендереров три варианта исходников — GLSL / HLSL / MSL; Metal-ветка выбирается по `sg_query_backend() == SG_BACKEND_METAL_MACOS` (runtime) или `#elif defined(SOKOL_METAL)` (compile-time). Единого `SG_BACKEND_METAL` в sokol нет.
- **sokol_app на macOS** требует Objective-C++: `main.cpp` standalone-приложений компилируется как ObjC++ через макрос `nw_configure_sokol_app(target)` в `cmake/utils.cmake` (там же линковка `Cocoa/QuartzCore/Metal/MetalKit`; на Windows — `d3d11/dxgi`). Все новые standalone sokol-приложения — через этот макрос.
- **glad нельзя включать в TU с `SOKOL_IMPL` на macOS и Linux** (и вообще вместе с Qt GL-заголовками): sokol_gfx/qopengl тянут системные GL-заголовки (`<OpenGL/gl3.h>` на macOS, `<GL/gl.h>` на Linux), которые конфликтуют с glad-макросами. На macOS это compile-time конфликт, на Linux — коварнее: glad'овский `__gl_h_` guard молча блокирует `<GL/gl.h>`, и sokol зовёт GL через незагруженные glad-указатели (NULL) → SIGSEGV в `sg_setup`. На Windows спасает встроенный лоадер sokol (его макросы перекрывают glad'овские). GLCORE-бэкенд линкуется `-framework OpenGL` на macOS и `OpenGL::GL` на Linux.
- **Ресурсы:** `baseDataPath` в `core_context.cpp` ищется вверх от cwd (repo root, `_intermediate_64` — оба сработают); хардкода абсолютных путей нет.
- **Код-подпись:** ad-hoc (`CODE_SIGN_IDENTITY "-"` в `nw_add_app_sources`, post-build `codesign -f -s -` у EpicMapEditor) — пустая identity в Xcode невалидна для CLI-таргетов.
- После обновления Xcode **сбрасывать CMake-кэш** (`cmake -U LIBRESOLV` или чистый `_intermediate_64`): `find_library` кэширует пути внутрь старого SDK.

## vcpkg и зависимости

- vcpkg подключён как submodule (`toolchain/vcpkg`). **Не патчить** `toolchain/vcpkg` напрямую.
- Зависимости — через `vcpkg.json`.
- Для overlay-фиксов — `vcpkg_overlays/ports`; путь уже подключён в `CMakePresets.json` через `VCPKG_OVERLAY_PORTS`.
  - Например, Sokol подключён через overlay, чтобы держать `util/sokol_imgui.h` совместимым с актуальным Dear ImGui из vcpkg.
- При добавлении зависимости: обновить `vcpkg.json` → `find_package(...)` → прилинковать импортированный target в `LIBS` у соответствующего `nw_add_...`.

### Binary cache (сетевой)

Все configure-пресеты в `CMakePresets.json` читают зависимости из общего HTTP binary cache (`https://cache.blackbox9.cc:9443`, readwrite) — чистая конфигурация после wipe `_intermediate_64` восстанавливает весь closure за ~2 минуты вместо полной пересборки. Авторизация **не хранится в репозитории**: пресет подставляет `$env{NEVERWHERE_VCPKG_CACHE_AUTH}`, поэтому на каждой машине нужен экспорт (shell profile / CI secret):

```bash
export NEVERWHERE_VCPKG_CACHE_AUTH="Authorization: Basic <base64(user:pass)>"
```

Переменная не установлена — не страшно: конфигурация не падает; чтение из кэша работает и без авторизации (GET на сервере открыт, проверено 2026-08-03 из LAN — см. `docs/VCPKG_CACHE.md`), но push не пройдёт: машина сможет потреблять кеш, но не пополнять. Подстановка `$env{}` работает только при запуске configure через пресет (`build_mac.sh`/`build_linux.sh`/`cmake --preset ...`); ZERO_CHECK-переконфигурация изнутри IDE берёт env процесса сборки.

## Индексация для IDE/clangd (Serena MCP, clangd LSP)

Основной флоу — Visual Studio generator, который **не** умеет эмиттить `compile_commands.json` (это ограничение CMake, не баг). Для Serena/clangd есть отдельный Ninja-preset, который только конфигурирует (не собирает).

- **Preset:** `ide-index` в `CMakePresets.json` — Ninja generator, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, отдельный `binaryDir = _intermediate_ide`.
- **Обёртка:** `generate_compile_commands.bat` — вызывает `vcvars64.bat` (Ninja не находит MSVC сам), конфигурит, копирует/симлинкает `compile_commands.json` в корень репо.
- **Запуск:** `generate_compile_commands.bat` (или `--clean` для полного ребилда кэша).
- **Когда перегенерировать:** после добавления/удаления исходников, изменения compile-флагов или `vcpkg.json`. После правок в существующих файлах — **не нужно** (clangd читает флаги из `compile_commands.json`, а не из самих исходников).
- `_intermediate_ide/` и `compile_commands.json` в `.gitignore` — это локальные artefacts.

Без `compile_commands.json` Serena работает на структурном parse (видит имена, но не типы), и `get_diagnostics_for_file` выдаёт ложный шум (`'QObject' file not found`, `Unknown type name 'Q_OBJECT'`). С ним — полноценная индексация: чистая диагностика, полные references, type-aware навигация.

## Linux: системные пакеты и особенности порта

- Системные зависимости (Ubuntu): `build-essential autoconf automake autoconf-archive libtool pkg-config bison flex gperf nasm python3 python3-venv` + X11/xcb/GL dev-стек для vcpkg-qtbase: `libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev libxtst-dev libxrandr-dev libxcursor-dev libxcomposite-dev libxdamage-dev libxrender-dev libxcb1-dev libxcb-glx0-dev libxcb-keysyms1-dev libxcb-image0-dev libxcb-shm0-dev libxcb-icccm4-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-shape0-dev libxcb-randr0-dev libxcb-render-util0-dev libxcb-util-dev libxcb-xinerama0-dev libxcb-xkb-dev libxcb-cursor-dev libxkbcommon-dev libxkbcommon-x11-dev libgl1-mesa-dev libglu1-mesa-dev libegl1-mesa-dev libfontconfig1-dev libfreetype6-dev libdbus-1-dev libsm-dev libxcb-xinput-dev`.

- **Рендер-бэкенд:** `SOKOL_GLCORE` везде (выбирается в `render_core/sokol_config.h` по умолчанию на не-win/apple/emscripten); шейдеры — GLSL-варианты. Внутри Qt-контекста (редактор, EcsPlayground) отдельный `QSurfaceFormat` не нужен: GLX по умолчанию даёт compat-контекст 4.x, sokol хватает (в отличие от macOS с legacy 2.1).
- **sokol_app на Linux** идёт через X11 (на Wayland-сессии — XWayland): макрос `nw_configure_sokol_app(target)` линкует `X11 Xi Xcursor GL dl pthread m`.
- **GLCORE в Qt-приложениях** (EpicMapEditor, EcsPlayground) требует `OpenGL::GL` на линковке — sokol_gfx грузит GL-функции через `glXGetProcAddress`.
- **RttrPlayground не собирается** (rttr windows-only; его CMakeLists возвращается сразу на не-Windows).
- `compile_commands.json` в корне — симлинк на `_int_linux/compile_commands.json` (preset сам эмиттит, отдельный index-preset не нужен).
- **RPC `screenshot` под Wayland отдаёт чёрное** ( `_cmdScreenshot` использует `QScreen::grabWindow` — X11-протокол, а под XWayland контент живёт в Wayland-буферах). **Штатный обход:** запускать редактор под `xvfb-run` (пакет `xvfb` установлен) — там настоящий X11-сервер и grabWindow честный:
  `xvfb-run -a -s "-screen 0 1920x1080x24" _int_linux/src/apps/EpicMapEditor/Debug/EpicMapEditor`
  Рендер внутри Xvfb — software (llvmpipe), кадры медленнее, чем на GPU; для скриншотов/авторинга через RPC этого хватает. На сами авторинг-операции (`set_tile`/`set_landscape`/…) платформа не влияет.
