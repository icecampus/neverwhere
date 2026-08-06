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
- `src/libs/highground_core` — генерация геометрии поднятой 3D-земли (стены + верх) по vertex-нодам; чистый конвейер `Grid + Params -> generate() -> Mesh`, без Qt/GPU. Там же (см. `cliff_field.h`/`surface_nets.h`) живёт cliff-field конвейер: сетка нод -> scalar field (плато + omphalos-борозды) -> surface nets -> watertight-mesh (используется кистью «Cliff 3D» в SDFGeneratedLandscape; standalone CliffFieldPlayground удалён за ненужностью — его сценарий живёт в `src/tests/highground/cliff_field_test.cpp`). Поднятая земля (`generate()`) общая для редактора/клиента (через `render_core`); cliff-field конвейер отлаживается в SDFGeneratedLandscape (кисть «Cliff 3D») — удобнее, чем в редакторе. Там же (`tech_field.h`) живёт tech-field конвейер — перенос семантики технических атласов TechnicalGrass Ridge/Valley (`utils/asset_generator/technical/`) в геометрию: fan-heightfield по нодам (4 угла + центр клетки), оба тайлсета сведены к одному генератору с блендом `style` через высоту центра (split Valley для Lack тождественен fan с центром=1); «стены» Python-атласов были артефактом экранной проекции, в 3D поверхность непрерывна по общим нодам; тёмный контур тайлов — shading-only groove-канал (creaseWidth). Используется кистью «Tech 3D» в SDFGeneratedLandscape (тот же кэш/debounce, что у «Cliff 3D»/«Stone 3D», своя земляная палитра через shading override), тесты — `src/tests/highground/tech_field_test.cpp`.
- `src/libs/landscape_mesh` — клеточная маска (`SolidMaskGrid`, см. `solidMaskFromNodes` для vertex-нод -> маска) -> `composeSolidMaskMesh()` -> plateau-полоса (верх + стены по границе, квады `MeshQuad` с запечённым цветом), один уровень = один вызов. Стили стен подключаемые: `IWallStyle` + `WallStyleId {BlockCliff, Cyclopean}` + `makeWallStyle()` (`wall_style.h`); `CyclopeanStyle` — порт Blender-прототипа `prototypes/blender/clifs/cyclopean_walls/clifs.blend`. Используется в PolygonalGeneratedLandscape; в `render_core`/редактор заведён как отдельный тип ассета `cyclopean3d` (`CyclopeanAsset`, слой `CyclopeanLandscape`, `render_core::CyclopeanRenderer` с хардкодом `WallStyleId::Cyclopean`, панель `CyclopeanSettings.qml`) — по шаблону интеграции cliff3d.
- stone3d-ассеты (`StoneAsset`, слой `StoneLandscape`, панель `StoneSettings.qml`) рендерятся тем же `render_core::CliffRenderer` (общие пайплайн/кэш/debounce): меш — через `stone_gen::StoneField` (блок `CliffParams::stoneField`, вершина с rim-атрибутом), шейдинг — stone-экстрами в `CliffFsParams::params4` (plane-Y гейт верхней палитры, rim-градиент к кромке, микс текстуры верха); чистый клиф при нулевых stone-каналах шейдится бит-в-бит как раньше. Сборка кадра — `WorldFrame::stoneTiles` + `WorldRenderer::ensureStoneAsset` (редактор `ModelFrameSource`, клиент/Play `world_frame_builder`; конвертеры `stoneParamsFromAssetData` рядом с `cliffParamsFromAssetData`). Cliff- и stone-тайлы идут в cliff-пасс **одним объединённым вызовом** `CliffRenderer::render` за кадр: у прохода per-frame scratch-буферы (превью-силуэты), а sokol разрешает лишь один `sg_update_buffer` на буфер за кадр — два вызова подряд под правками в обоих слоях давали `VALIDATE_UPDATEBUF_ONCE`.
- texture2d-ассеты (`TextureAsset`, слой `TextureLandscape`, payload `"texture2d"` в index.json: файл тайлящейся текстуры + `tilingRepeats`; v1 без QML-панели) — порт слоя «Texture 2D» из SDFGeneratedLandscape: плоская земля с world-UV тайлящимися текстурами, плавный бленд соседних текстур и растворение контура в пустоту. Рендер — `LandscapeRenderer::renderTexture` (вызывается из `WorldRenderer::render` сразу после плоской земли, та же константная глубина 0.999 и stitch-шейдинг): все текстуры живут в одном texture array (по срезу на ассет, `ensureTextureAsset`, ребилд лениво при смене набора), клетка тесселлируется веером (центр + 4 угловые ноды) с one-hot весами кандидатов и fill-весом, FS — порт плейграундного (микс по весам с fbm-вобблом и sharpness, alpha = smoothstep вокруг iso 0.5). Per-node тегов текстур в карте НЕТ (хранится per-cell tileIndex + assetUuid): текстура ноды восстанавливается голосованием — `render_core/texture_blend.h` `buildTextureBlendCells` (каждая клетка голосует своим assetUuid за включённые углы; ничья — в пользу кандидата с более поздним первым голосом ≈ last-paint-wins), покрыто `src/tests/render_core/texture_blend_test.cpp`. Бленд-параметры (sharpness/noise/edgeFade) — публичное поле `WorldRenderer::textureBlend` (дефолты плейграунда, юниформы, v1 без QML), тайлинг — per-asset из JSON (едет в вершине). Кисть — `TexturePencil` (наследник `LandscapePencil`, логика без изменений: assetUuid per cell пишется сам), данные — те же Landscape-тайлы. Бандлы-примеры: `resources/assets/landscape/TextureGrass`, `TextureRock`.
- Эффекты стыка хайграунда с землёй портированы из HighgroundWithEffects в `render_core` (общие для редактора/клиента): `render_core/scene_stitch.h` (CPU-половина: `buildContactAoField`, `SceneStitchSettings`/`SceneStitchParams`, `SeamParams`, `kStitchCoreBytes`) + `WorldRenderer::prepare(frame, nowSec)` (вызывается шеллом **до** `sg_begin_pass`: пересобирает объединённый нод-футпринт raised+cliff+stone+cyclopean в R8 AO-текстуру при смене контент-ключа; пустое поле → placeholder 1×1=255; владеет AO image/view/sampler и shared stitch-блоком). Настройки — публичные поля `WorldRenderer::stitch`/`seam` (дефолты плейграунда, v1 без QML). Земля (`LandscapeRenderer::render`, сигнатура с `GroundStitchContext`) несёт world-координаты в вершине, сидит на константной глубине 0.999 (depth write ON, LESS_EQUAL — тайлы земли разрешаются painter-порядком, 3D всегда поверх) и шейдится общим солнцем/тоном + контактным AO (полный 64-байтный stitch-блок с `aoRect`); старый per-node `CliffShadowField` удалён. Cliff-пасс берёт солнце из stitch-ядра (48 байт, слот 2; per-asset `lightAzimuth/lightElevation` в `CliffShading` оставлены для JSON-совместимости, но не используются) и несёт seam-каналы в `CliffFsParams::params5..params8` + яркость верха в `params3.y` (rim contact AO, bounce/sky тинты, тинт/яркость/rotation верха, foot AO по `stitch1.z`; нулевые силы = поведение как до порта). Грабли: в GLSL stitch-пара после `sun_dir` объявлена массивом `uniform vec4 stitch[2]` — драйвер выкидывает неиспользуемые именованные юниформы (ground-only `stitch0` в cliff FS не потребляется), и sokol рапортует `GL_UNIFORMBLOCK_NAME_NOT_FOUND_IN_SHADER`; массив атомарен, пока жив хоть один элемент.
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
- **CLion:** пресет `macos-clion` (Ninja, `_int_clion`, Debug + compile_commands) — CLion не поддерживает Xcode-генератор основного пресета. `macos-index` (`_intermediate_ide`) оставлен под индексацию Serena/clangd, в IDE его не используем.
- Бинарники: `_intermediate_64/src/apps/<App>/Debug/<App>` (command-line executables, не .app).
- Smoke-проверки: `EpicGameClient --smoke`, `EpicMapEditor --smoke`; юнит-тесты — `_intermediate_64/src/tests/Debug/neverwhere_tests`.
- **RttrPlayground на macOS не собирается** (vcpkg-порт rttr не поддерживает osx; его CMakeLists возвращается сразу на не-Windows).

Платформенные особенности порта:

- **Рендер-бэкенды:** редактор и EcsPlayground — принудительный `SOKOL_GLCORE` (Qt OpenGL контекст), standalone-приложения — `SOKOL_METAL` (выбор в `render_core/sokol_config.h` и main.cpp по `__APPLE__`). Для GLCORE на macOS обязателен core profile: `QSurfaceFormat` 4.1 Core до создания `QApplication` (см. `main.cpp` редактора), иначе Qt даёт legacy 2.1 контекст и sokol падает на `sg_setup()` в Debug.
- **Шейдеры:** у `render_core` и playground-рендереров три варианта исходников — GLSL / HLSL / MSL; Metal-ветка выбирается по `sg_query_backend() == SG_BACKEND_METAL_MACOS` (runtime) или `#elif defined(SOKOL_METAL)` (compile-time). Единого `SG_BACKEND_METAL` в sokol нет.
- **sokol_app на macOS** требует Objective-C++: `main.cpp` standalone-приложений компилируется как ObjC++ через макрос `nw_configure_sokol_app(target)` в `cmake/utils.cmake` (там же линковка `Cocoa/QuartzCore/Metal/MetalKit`; на Windows — `d3d11/dxgi`). Все новые standalone sokol-приложения — через этот макрос.
- **glad нельзя включать в TU с `SOKOL_IMPL` на macOS и Linux** (и вообще вместе с Qt GL-заголовками): sokol_gfx/qopengl тянут системные GL-заголовки (`<OpenGL/gl3.h>` на macOS, `<GL/gl.h>` на Linux), которые конфликтуют с glad-макросами. На macOS это compile-time конфликт, на Linux — коварнее: glad'овский `__gl_h_` guard молча блокирует `<GL/gl.h>`, и sokol зовёт GL через незагруженные glad-указатели (NULL) → SIGSEGV в `sg_setup`. На Windows спасает встроенный лоадер sokol (его макросы перекрывают glad'овские). GLCORE-бэкенд линкуется `-framework OpenGL` на macOS и `OpenGL::GL` на Linux.
- **Ресурсы:** `baseDataPath` в `core_context.cpp` ищется вверх от cwd (repo root, `_intermediate_64` — оба сработают); хардкода абсолютных путей нет.
- **Код-подпись:** ad-hoc (`CODE_SIGN_IDENTITY "-"` в `nw_add_app_sources`, post-build `codesign -f -s -` у EpicMapEditor) — пустая identity в Xcode невалидна для CLI-таргетов.
- После обновления Xcode **сбрасывать CMake-кэш** (`cmake -U LIBRESOLV` или чистый `_intermediate_64`): `find_library` кэширует пути внутрь старого SDK.

### vcpkg и зависимости

- vcpkg подключён как submodule (`toolchain/vcpkg`). **Не патчить** `toolchain/vcpkg` напрямую.
- Зависимости — через `vcpkg.json`.
- Для overlay-фиксов — `vcpkg_overlays/ports`; путь уже подключён в `CMakePresets.json` через `VCPKG_OVERLAY_PORTS`.
  - Например, Sokol подключён через overlay, чтобы держать `util/sokol_imgui.h` совместимым с актуальным Dear ImGui из vcpkg.
- При добавлении зависимости: обновить `vcpkg.json` → `find_package(...)` → прилинковать импортированный target в `LIBS` у соответствующего `nw_add_...`.

#### Binary cache (сетевой)

Все configure-пресеты в `CMakePresets.json` читают зависимости из общего HTTP binary cache (`https://cache.blackbox9.cc:9443`, readwrite) — чистая конфигурация после wipe `_intermediate_64` восстанавливает весь closure за ~2 минуты вместо полной пересборки. Авторизация **не хранится в репозитории**: пресет подставляет `$env{NEVERWHERE_VCPKG_CACHE_AUTH}`, поэтому на каждой машине нужен экспорт (shell profile / CI secret):

```bash
export NEVERWHERE_VCPKG_CACHE_AUTH="Authorization: Basic <base64(user:pass)>"
```

Переменная не установлена — не страшно: конфигурация не падает; чтение из кэша работает и без авторизации (GET на сервере открыт, проверено 2026-08-03 из LAN — см. `docs/VCPKG_CACHE.md`), но push не пройдёт: машина сможет потреблять кеш, но не пополнять. Подстановка `$env{}` работает только при запуске configure через пресет (`build_mac.sh`/`build_linux.sh`/`cmake --preset ...`); ZERO_CHECK-переконфигурация изнутри IDE берёт env процесса сборки.

### Индексация для IDE/clangd (Serena MCP, clangd LSP)

Основной флоу — Visual Studio generator, который **не** умеет эмиттить `compile_commands.json` (это ограничение CMake, не баг). Для Serena/clangd есть отдельный Ninja-preset, который только конфигурирует (не собирает).

- **Preset:** `ide-index` в `CMakePresets.json` — Ninja generator, `CMAKE_EXPORT_COMPILE_COMMANDS=ON`, отдельный `binaryDir = _intermediate_ide`.
- **Обёртка:** `generate_compile_commands.bat` — вызывает `vcvars64.bat` (Ninja не находит MSVC сам), конфигурит, копирует/симлинкает `compile_commands.json` в корень репо.
- **Запуск:** `generate_compile_commands.bat` (или `--clean` для полного ребилда кэша).
- **Когда перегенерировать:** после добавления/удаления исходников, изменения compile-флагов или `vcpkg.json`. После правок в существующих файлах — **не нужно** (clangd читает флаги из `compile_commands.json`, а не из самих исходников).
- `_intermediate_ide/` и `compile_commands.json` в `.gitignore` — это локальные artefacts.

Без `compile_commands.json` Serena работает на структурном parse (видит имена, но не типы), и `get_diagnostics_for_file` выдаёт ложный шум (`'QObject' file not found`, `Unknown type name 'Q_OBJECT'`). С ним — полноценная индексация: чистая диагностика, полные references, type-aware навигация.

## Сборка (Linux, CMake + vcpkg)

Linux-флоу — Ninja (single-config) + CMake Presets, триплет `x64-linux`, бинарная директория `_int_linux` (**не** `_intermediate_64` — та под win/mac-кэш).

- Системные зависимости (Ubuntu): `build-essential autoconf automake autoconf-archive libtool pkg-config bison flex gperf nasm python3 python3-venv` + X11/xcb/GL dev-стек для vcpkg-qtbase: `libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev libxtst-dev libxrandr-dev libxcursor-dev libxcomposite-dev libxdamage-dev libxrender-dev libxcb1-dev libxcb-glx0-dev libxcb-keysyms1-dev libxcb-image0-dev libxcb-shm0-dev libxcb-icccm4-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-shape0-dev libxcb-randr0-dev libxcb-render-util0-dev libxcb-util-dev libxcb-xinerama0-dev libxcb-xkb-dev libxcb-cursor-dev libxkbcommon-dev libxkbcommon-x11-dev libgl1-mesa-dev libglu1-mesa-dev libegl1-mesa-dev libfontconfig1-dev libfreetype6-dev libdbus-1-dev libsm-dev libxcb-xinput-dev`.
- Конфигурация: `./build_linux.sh` (обёртка над `cmake --preset linux`). Первая конфигурация собирает все vcpkg-зависимости (включая Qt) — ~1–2 часа.
- Сборка из CLI: `cmake --build --preset linux-debug --target EpicMapEditor`.
- Бинарники: `_int_linux/src/apps/<App>/Debug/<App>` (app-макросы кладут exe в подпапку `$<CONFIG>`, как VS/Xcode — иначе на single-config Ninja exe коллидирует с директорией qml-модуля того же имени).
- Smoke-проверки и юнит-тесты — как на macOS: `EpicMapEditor --smoke`, `_int_linux/src/tests/neverwhere_tests` (ctest: `ctest --test-dir _int_linux --output-on-failure`).

Платформенные особенности порта:

- **Рендер-бэкенд:** `SOKOL_GLCORE` везде (выбирается в `render_core/sokol_config.h` по умолчанию на не-win/apple/emscripten); шейдеры — GLSL-варианты. Внутри Qt-контекста (редактор, EcsPlayground) отдельный `QSurfaceFormat` не нужен: GLX по умолчанию даёт compat-контекст 4.x, sokol хватает (в отличие от macOS с legacy 2.1).
- **sokol_app на Linux** идёт через X11 (на Wayland-сессии — XWayland): макрос `nw_configure_sokol_app(target)` линкует `X11 Xi Xcursor GL dl pthread m`.
- **GLCORE в Qt-приложениях** (EpicMapEditor, EcsPlayground) требует `OpenGL::GL` на линковке — sokol_gfx грузит GL-функции через `glXGetProcAddress`.
- **RttrPlayground не собирается** (rttr windows-only; его CMakeLists возвращается сразу на не-Windows).
- `compile_commands.json` в корне — симлинк на `_int_linux/compile_commands.json` (preset сам эмиттит, отдельный index-preset не нужен).
- **RPC `screenshot` под Wayland отдаёт чёрное** ( `_cmdScreenshot` использует `QScreen::grabWindow` — X11-протокол, а под XWayland контент живёт в Wayland-буферах). **Штатный обход:** запускать редактор под `xvfb-run` (пакет `xvfb` установлен) — там настоящий X11-сервер и grabWindow честный:
  `xvfb-run -a -s "-screen 0 1920x1080x24" _int_linux/src/apps/EpicMapEditor/Debug/EpicMapEditor`
  Рендер внутри Xvfb — software (llvmpipe), кадры медленнее, чем на GPU; для скриншотов/авторинга через RPC этого хватает. На сами авторинг-операции (`set_tile`/`set_landscape`/…) платформа не влияет.

## Undo/Redo

- Предпочитать immutable-снэпшоты через `immer`; избегать Command Pattern, если явно не просят. (Сейчас immer — `[концепт]`, см. `docs/TECHNICAL_STACK.md`.)

## Тестирование и прототипы

- Unit-тесты (gtest) — `src/tests/<domain>/<name>_test.cpp`, подхватываются GLOB'ом автоматически; бинарь `neverwhere_tests`.
  - Сборка: `cmake --build --preset debug --target neverwhere_tests`.
  - Запуск: `ctest --test-dir _intermediate_64 -C Debug --output-on-failure` или напрямую `_intermediate_64/Debug/neverwhere_tests.exe`.
- Новые тестовые приложения и прототипы — под `src/apps/<PrototypeName>`; ландшафтные плейграунды вынесены в отдельную группу `src/landscape_playgrounds/` (HighgroundWithEffects, PolygonalGeneratedLandscape, SDFGeneratedLandscape, Shadertoy, StoneCube, TextureBlendLandscape) — подключается `add_subdirectory(src/landscape_playgrounds)` в корневом CMakeLists, внутри тот же GLOB по подпапкам, таргет = имя директории приложения.
- Таргеты прототипов/тестов **обязаны** иметь суффикс `Playground` (напр. `RttrPlayground`, `EcsPlayground`) — кроме группы `src/landscape_playgrounds/`: там принадлежность видна по корневой папке, поэтому суффикс у приложений убран.
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

Имена слоёв для оп: `Decoration` | `BaseLandscape` | `GameplayInteractive` | `RaisedLandscape` | `CliffLandscape` | `CyclopeanLandscape` | `StoneLandscape` | `TextureLandscape` (enum `LayerTypes`). Запись идёт через `MapAuthoring` (`src/libs/core/map/map_authoring.h`), он же покрыт gtest (`src/tests/map/map_authoring_test.cpp`). `set_landscape` целится в слой по `layerType` ассета: slice-ассеты → `BaseLandscape`, shape3d-ассеты (поднятая 3D-земля) → `RaisedLandscape`, cliff3d-ассеты (клифы surface-nets) → `CliffLandscape`, cyclopean3d-ассеты (кладка landscape_mesh) → `CyclopeanLandscape`, stone3d-ассеты (вороной-камни `stone_gen::StoneField`) → `StoneLandscape`, texture2d-ассеты (тайлящиеся текстуры с блендом) → `TextureLandscape`.

MCP-обёртка: серверы `neverwhere-editor` (macOS) / `neverwhere-editor-win` в `.mcp.json`, код — `tools/editor_mcp/` (тонкий прокси, инструменты `editor_*`; TCP-клиент переиспользован из `tools/debug_mcp/editor_rpc_client.py`, пригоден и для ручных скриптов). Типовой цикл «карта по описанию»: `create_chapter`/`load_chapter` → `list_assets` → `set_landscape` → `fill_rect`/`set_tile` → `set_camera` + `screenshot` → `get_map` → `save` → `play`.

### Shadertoy

`src/landscape_playgrounds/Shadertoy` — универсальный хост для shadertoy-референсов из `docs/reference/shadertoy` (дорога «демка → кубик с материалом → SDFGeneratedLandscape → инструмент → редактор»; это шаг 1–2). Демки подхватываются без перекомпиляции: пассы по именам (`Image.glsl`, `BufferA..D.glsl`, `Common.glsl` инжектится во все пассы), текстуры `textures/iChannelN.(png|jpg)` биндятся по имени файла, multi-pass граф — опциональный `shadertoy.json` (формат — в `docs/reference/shadertoy/README.md`). ImGui-список демок, hot-reload (R / кнопка; ошибки компиляции — в окне), превью «кубик с материалом» (Image-пасс на вращающемся кубе), `--list` / `--demo "name"` / `--cube` / `--dir` / `--shot <png>` / `--scale <0.1..1.0>` (рендер в пониженном разрешении с апскейлом — реймарч-демки на 2x-DPI фреймбуфере 2560×1440 иначе ползут; время кадра линейно по пикселям, GPU-bound) / `--smoke` (прогон всех демок по 30 кадров с TEST PASS/FAIL).

- **Бэкенд — осознанное исключение:** `SOKOL_GLCORE` на всех desktop-платформах (не Metal на macOS): демки — single-source GLSL `#version 330` + shim (iResolution/iTime/iMouse/…/iChannel0-3), портить каждую на 3 диалекта нерентабельно. На MSVC — `opengl32`, на macOS — `-framework OpenGL` (в CMakeLists поверх `nw_configure_sokol_app`).
- **Заголовки с `#include <sokol_gfx.h>`** (ShadertoyRuntime.h, CubePreview.h) должны включаться в TU **до** `#define SOKOL_IMPL` — в этой версии sokol impl-секция живёт вне include-гарда и двойное включение с SOKOL_IMPL даёт redefinition-ошибки (конвенция как в TextureBlendLandscape).
- **macOS-особенность GLSL:** Apple-компилятор резервирует `noise1..4` (legacy built-ins) — shim переименовывает их макросами в преамбуле; `pow(vecN, float)` нестандартен (ANGLE терпит, Apple — нет), правится в исходнике демки.
- Пайплайны: буферы — RGBA16F (feedback-точность), Image — формат свапчейна (и image-target для куба в нём же); все пассы с общим depth-attachment'ом, чтобы один depth-формат подходил везде.

### StoneCube + stone_gen

`src/landscape_playgrounds/StoneCube` — инкубатор генератора «кубика из камней» по принципам iq'шной демки Voronoi - rocks (`docs/reference/shadertoy`): SDF = round-box + bulge внутри voronoi-ячеек (`clamp(k*(F2-F1))`, борозды по границам), fbm-деталь + bump, AO + soft shadow, ground plane с тенью, orbit-камера (LMB — вращение, RMB — пан, колесо — зум), ImGui-параметры (Shape/Detail/Look, кнопка New seed). Дорога развития: C++-двойник SDF → меш через surface-nets (`highground_core`) → SDFGeneratedLandscape → редактор.

`src/libs/stone_gen` — no-GPU либа генератора и пекарни (паттерн `highground_core`): `StoneSdf` (C++-двойник, каноничен; GLSL в плейграунде — look-референс) → `generateMesh` (sample → `regularizeSigns`/`extractSurfaceNets` через `cliff::ScalarFieldView` — обобщённая точка входа в `surface_nets.h`, клифы делегируют в неё) → `bakeTextures` (CPU-растеризатор в UV-пространстве: `albedo.png` rgb=тинт+moss+groove, a=AO; `normal.png` object-space) + `writeObj`/`writePng`. UV — атлас 3×2: box-проекция по доминантной оси нормали, у каждой из 6 граней свой тайл (проекция всех граней в общий [0,1] наслаивает их данные бейка друг на друга) + дедуп швов (`splitSeamVertices`: клонирует углы треугольников, чья грань (ось+знак) не совпадает с гранью треугольника, иначе они тянутся через весь атлас). Покрытие — `src/tests/stone/stone_gen_test.cpp` (watertight, бейк, экспорт).

Там же (`stone_field.h`) живёт `StoneField` — перенос каменного принципа на хайграунд по нодам (контракт как у `cliff::CliffField`): сетка нод → плита-плато (база `cliff::CliffField::evalBase`) → voronoi-карвинг StoneCube (`clamp(k*(F2-F1))` bulge, борозды по границам ячеек, маской прижат к поверхности плиты) + fbm → surface nets. По умолчанию включён flat-top с rim-стичем: рельеф (карвинг + fbm + groove-атрибут шейдинга) гасится по Y-компоненту нормали базовой плиты (`flatTop`/`flatTopLo`/`flatTopHi`), но в полосе у кромки (`rimWidth`, в ~мировых единицах — `(edgeRadius-d2)/|∇d2|`, т.к. d2 — сжатый псевдо-SDF, а не дистанция) стыковка структурная: камни не срезаются, а заворачиваются на верх (`rimBulge`), устья борозд/провалов вдавливают верх вниз (`rimNotch`); середина верха остаётся точно плоской. Численные градиенты — общий 7-таповый `StoneField::sampleBase`; двухаргументный `CliffField::evalBase(p, d2)` публичный. Используется кистью «Stone 3D» в SDFGeneratedLandscape (тот же кэш/debounce, что у «Cliff 3D», свои параметры `StoneFieldParams` и серая каменная палитра через per-layer shading override; выступы-валуны над плоскостью верха красятся палитрой стены — порог plane-Y в поле-юниформе `params1.w` общего cliff-шейдера, 0 = выкл, клиф-слой не затронут; впадины кромки остаются палитрой верха; градиент «камень к кромке» на плоском верхе идёт per-vertex rim-атрибутом (`StoneField::rimFactor` — близость к стене из `rimStitch`, без гейта по topness: стены шейдер сам отсекает нормалью; транспорт — `cliff::ScalarFieldView::rimFactor` → `cliff::MeshVertex::rim`, сила — юниформа `params2.y`, ширина — общий `rimWidth`), верх опционально заливается тайлящейся текстурой (`top_tex`, world-space UV по `p.xz`, сила/тайлинг — `params2.zw`; в плейграунде грузится `resources/textures/grass.png`, при промахе — палитра); pcg3d/voronoi/fbm — бит-в-бит дубли `stone_sdf.cpp`, StoneSdf не трогаем).

- Playground: режимы Raymarch | Mesh (запечённые текстуры) | **Procedural** — тот же меш, но материал (voro-тинт/мох/fbm-бамп/AO/soft shadow) считается попиксельно из world-позиции, без UV и текстур (подход `cliff_renderer`; look-параметры живые, ребилд меша только по shape-параметрам, debounce 0.3 с). Процедурный и реймарч режимы рендерятся в scaled offscreen target (тяжёлые попиксельные шейдеры на 2x-DPI). Bake 256/512/1024, Export в `bake_out/`, CLI `--smoke` / `--shot` / `--scale` / `--seed` / `--mesh` / `--proc` / `--bake <dir>`.
- Грабли (по опыту): naive surface nets террасит на крутых voronoi-бороздах — лечится 3-tap блюром сэмплов (`MeshParams::blurPasses`); bump-градиент fbm десятки единиц — без кепа `g/(1+|g|)` запечённая normal-map выходит радужным шумом; на бейке stencil градиента ~1–2 текселя (low-pass); GLSL 330 `const float x = <uniform-выражение>` не компилируется.
- Шейдерные: полосы «шторки» на реймарче — это квантование глубины попадания (raymarch banding), лечится секант-уточнением хита + широким stencil нормали (0.004), а НЕ более мелким eps; `fract(sin(x)*43758)` на больших аргументах даёт направленные полосы — в генераторах использовать целочисленный pcg3d-хэш (GLSL и C++ дублируют друг друга бит-в-бит).
- stb-имплементации (STB_IMAGE_..._IMPLEMENTATION) — в одном TU на бинарь: для `stone_gen` это `stone_bake.cpp`, приложения либу линкуют и не повторяют.

### HighgroundWithEffects: стыковка хайграунда с землёй

`src/landscape_playgrounds/SDFGeneratedLandscape` раздвоён: **SDFGeneratedLandscape** — чистый стенд отладки генерации геометрии (состояние до эффектов стыковки), **HighgroundWithEffects** — форк с эффектами стыка (общее солнце, контактное AO, shadow map, материалы стыка, grass underlay; также HiDPI-фикс сцены в точках). Геометрические кисти («Cliff 3D», «Stone 3D») и конвейеры одинаковы в обоих; эффекты — только в Effect-версии.

У SDFGeneratedLandscape есть плоский слой «Texture 2D»: ландшафт из любой тайлящейся текстуры (`resources/textures`, подхват всех png/jpg на старте; все файлы пакуются в один texture array — `AtlasRenderer::buildTilingTextureArray`, билинейный ресемпл к общему размеру, слой массива = индекс файла). Текстура **не нарезается** на per-tile кусочки атласа (кусочки не сойдутся на стыках) — цвет сэмплится непрерывно в мировых (field) координатах (`v_world * tiling`, REPEAT-сэмплер, как `top_tex` у клифов), а Yellow-маска Flat-атласа работает только stencil (alpha), без рельефа кромок — поверхность читается как одно целое (у самого Yellow 2D тёмные окантовки ромбов — осознанный технический арт). В палитре каждая текстура — отдельный инструмент, сгруппированный под «Texture 2D», чтобы было видно, что слой один: нода хранит тег текстуры (`LandBrush::setNode(node, on, tag)`, tag = слой массива + 1, 0 = untagged), перекраска ноды другой текстурой вытесняет старую. Переходы между соседними текстурами — плавные: клетка тесселлируется веером из центра в 4 угловые ноды (ромб, `appendTileFan`), углы несут one-hot веса кандидатных текстур клетки (`LandBrush::cellTextureBlend`, ≤4 кандидатов; соседние клетки непрерывны, т.к. делят ноды на общем ребре), а FS миксует слои массива по интерполированным весам с органичным fbm-вобблом (гейт `w*(1-w)` — чистые интерьеры не трогает) и sharpness-экспонентой. Тем же веером растворяется и внешний контур региона: per-vertex fill (1 у on-ноды, 0 у off) интерполируется, альфа = smoothstep вокруг iso 0.5 (та линия, где раньше резала маска) с тем же fbm-вобблом — жёстких ступенек на границе с пустотой нет; ширина пера — слайдер edge fade (`blend_params.w`, 0 = жёсткий срез). Плоские не-текстурные слои (Grass/Yellow/силуэты клифов) идут квадом с fill=1 и не меняются. Все blend-параметры — живые юниформы (`blend_params`, слайдеры у слоя). Мажоритарное `cellTagAt` осталось для логики/фолбэка, рендер идёт через бленд; untagged-клетки (нулевые веса) падают в маску-only фолбэк. Реализация — world-UV режим плоского прохода `AtlasRenderer` (юниформы `tex_params`/`blend_params`; у мультитекстурного слоя `PaintLayerView::multiTexture` — один range на слой); CLI `--tex-nodes=` / `--tex-tiling=`. `--shot` там портируемый (glReadPixels, как у Effect-версии); захват ждёт и 120 кадров, и 1 с wall-time — на быстрой машине кадры пролетают быстрее дебаунса 0.3 с и 3D-меши не успевают в кадр.

Хайграунд «висел в воздухе»: земля рисовалась сырой текстурой рядом с освещённым мешем, без единого света, теней и AO. Сейчас оба пасса живут в одной световой модели (`AtlasRenderer` + `SceneStitch.h/.cpp`):

- **Кадр — два этапа.** `AtlasRenderer::prepare(SceneFrame)` (перестройка меш-кэшей, AO-поле, shadow-пасс) вызывается **до** открытия swapchain-пасса, `render(SceneFrame)` — внутри него. Offscreen-пасс внутри swapchain-пасса открыть нельзя, поэтому разделение обязательное.
- **Общий блок юниформ** `SceneStitchParams` (солнце, ортобазис тени, тон, силы AO/тени) идёт в оба FS. Ground-пасс берёт его целиком, cliff-пасс — только «ядро» (`kStitchCoreBytes`, без `ao_rect`): GL-драйвер выкидывает неиспользуемые юниформы, и sokol на каждом старте ругался `GL_UNIFORMBLOCK_NAME_NOT_FOUND_IN_SHADER`. По той же причине у cliff-пасса нет своего `light_dir` — солнце одно на сцену.
- **Земля пишет глубину** (`kGroundDepth`, одна константа за всей сценой) и несёт мировые координаты в `TexVertex` — `worldFromField()` инвертирует `DiamondIsometry::nodeToField`, так что фрагмент земли знает свою клетку и может сэмплить AO-поле и shadow map в кадре меша.
- **Контактное AO** — CPU chamfer distance transform от контура хайграунда (`buildContactAoField`, кэш по `LandBrush::version()`), выгружается в R8; в шейдере расстояние нормировано `kAoMaxDistanceCells`, ширина полосы — `aoRadius` (в клетках), сила — `aoStrength`. Та же сила поднимается по самой стене (`aoWallFade` — высота затухания в единицах меша, едет в cliff-шейдер через `CliffFsParams::params7.w`): если затемнять только землю, камень прямо над ней остаётся светлым и шов, который AO прячет, наоборот подчёркивается. Ключевое: маска берётся **субклеточная**, по тому же правилу частичного заполнения, что рисуют превью-тайлы (`diamondNodeFill` в `LandBrush.h`, общая с `FlatAtlasGenerator`) — клетка это ромб с нодой в каждом углу, и наполовину зажжённая клетка залита клином, а не целиком. С побитовой «клетка занята / не занята» затемнение отступает от диагональной стены на целую клетку и обводит границы клеток, а не сам клиф. Отсюда 8 текселей на клетку (клин у половинной клетки идёт через центр) и дробный посев chamfer'а по линейному пересечению нуля: округление контура до целых текселей даёт лесенку на косых стенах.
- **Shadow map выключен по умолчанию** (`SceneStitchSettings::shadowsEnabled = false`, пасс при этом даже не рендерится): жёсткая солнечная тень поперёк тайлов спорит с освещением, уже запечённым в атласной графике, а стыковку тянет контактное AO. Сама реализация оставлена под галкой — ортопроекция солнца (`buildSunBasis`) в **R32F color attachment** (+ depth attachment для видимости): depth-only пассов и сэмплирования depth-текстур в репо нет вообще. Сэмплер обязан быть `NONFILTERING` / `UNFILTERABLE_FLOAT` (float-таргет не гарантированно фильтруемый), PCF 3×3 руками. Высота меша приводится к клеточным единицам через `isoHeightToWorld(halfW, halfH, heightScale)`, иначе тень не совпадёт по длине с силуэтом.
- **Материалы стыка** (юниформы, без ребилда меша): рефлекс травы снизу и тон неба сверху, затемнение кромки по per-vertex `rim`, у верха плато свои тинт/яркость/тайлинг/поворот UV — он делит `grass.png` с землёй и без этого читается как её продолжение.
- **Два эффекта выключены по умолчанию, но код оставлен:** травяная юбка на подошве стены (`SeamParams::overgrowth = 0`) даёт плоские зелёные кляксы по камню, читающиеся как ошибка текстуры, а кольцо валунов (`ScreeParams::enabled = false`, `appendScreeRing` — дописывает валуны в тот же cliff-поток вершин с `groove = -1` как маркером «палитра стены») рассыпает одинаковую процедурную гальку. И то и другое подножие хочет получить геометрией: отдельной кистью пропов с набором, который подбирает художник под окружение. В шейдере у юбки есть явный гейт `step(1e-4, params4.x)` — без него `step()` против нулевого порога всё равно срабатывает там, где шум ровно ноль.
- **Направление солнца** правит ламбертом обоих пассов и (если тень включить) её длиной: азимут 3π/4 кладёт тень горизонтально по экрану — иначе она прячется за силуэтом меша, который поднят вверх, — а низкая высота даёт ей длину, на которой её видно.
- Проверка: `--smoke` (в т.ч. CPU-проверки AO-поля и ортонормированности базиса), `--shot` теперь портируемый (glReadPixels в TU с `SOKOL_IMPL`; glad подключать нельзя). На Linux скриншоты — под `xvfb-run`. На macOS у Metal `--shot` нечем читать (readback только в GLCORE-ветке), а системный `screencapture` из терминала без Screen Recording-пермишена отдаёт чёрный кадр — для скриншотов standalone-плейграундов на macOS переконфигурировать с GLCORE-бэкендом (у SDFGeneratedLandscape есть opt-in `-DSDFGL_FORCE_GLCORE=ON`, sokol_app на macOS умеет GL); после проверки вернуть Metal-дефолт.

**HiDPI в sokol_app + ImGui (грабли, стоившие «кисть красит не там, где курсор»).** `sapp_width()/sapp_height()` и `ev->mouse_x/y` — **пиксели фреймбуфера**, а ImGui (и, значит, ширина панели) живёт в **логических точках**: `simgui_new_frame()` сам делит размер на `dpi_scale`, а `simgui_handle_event()` — координаты мыши. Стоит вычесть ширину панели (точки) из позиции мыши (пиксели), и на `dpi_scale = 2` курсор уезжает на пол-панели, а viewport, посчитанный как «(пиксели − точки) × dpi», раздувается вдвое. В HighgroundWithEffects вся сцена (камера, мышь, `SceneFrame::viewW/viewH`) теперь в точках — `dpiScale()`/`windowSize()` в `main.cpp`, — а в пиксели переходит ровно одно место, `sg_apply_viewport`, плюс `glReadPixels` на скриншоте. Проверять это под Xvfb можно: `dpi_scale` sokol берёт из `Xft.dpi` в свойстве `RESOURCE_MANAGER` корневого окна, а голый Xvfb **сбрасывается при отключении последнего клиента** и свойство теряет — ставить его и запускать приложение надо с одного живого X-соединения (питоновский `Xlib` + `xtest` для синтетических кликов).

## Где что искать

- `README.md` — что это за проект, высокоуровневая архитектура.
- `docs/ROADMAP_VISION.md` — продуктовое видение и статусы фич.
- `docs/TECHNICAL_STACK.md` — стек и решения (что есть / прототип / концепт).
- `docs/reference/` — бумаги и порты по процедурному рельефу.
- `docs/SDF_TO_MESH_PLAYBOOK.md` — рецепт переноса shadertoy SDF-демки в меш-генератор (C++-двойник, surface nets, процедурный материал, грабли).
- `docs/debugging.md` — отладка через cdb-MCP: flow, gotchas, first-chance exceptions, чек-лист.
