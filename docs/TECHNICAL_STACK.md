# Технический стек и архитектурные решения

> **Легенда статусов:**
> - `[есть]` — реально используется в продакшн-коде;
> - `[прототип]` — проверено в песочнице (`src/apps/*Playground`), в основной код не въехало;
> - `[концепт]` — запланировано, в коде отсутствует.
>
> Статусы сверены с репозиторием на дату обновления документа.

## Базовый стек `[есть]`

| Назначение | Технология | Где живёт |
|---|---|---|
| Язык | C++20 (MSVC; Clang для Emscripten) | — |
| Сборка | CMake 3.20+, vcpkg (submodule), presets `vs2022`/`emscripten` | `CMakePresets.json`, `cmake/utils.cmake` |
| Рендер | Sokol GFX (`sokol_gfx`), `sokol_app`/`sokol_time`/`sokol_glue` | `src/libs/render_core` (общий рендер мира), GL-клей для Qt — внутри `EcsPlayground`, apps + playgrounds |
| GL-загрузчик | glad (Windows) | — |
| Сериализация | nlohmann::json | `src/libs/game_data` |
| Математика | glm | `src/libs/math` |
| Шум | fastnoise2, libnoise | `src/libs/generators`, landscape |
| Логирование | spdlog (non-emscripten) | — |
| Прочее | magic_enum, Boost (uuid, lexical_cast, container_hash), stb | — |
| UI редактора | Qt6 (qml, quick, widgets, quickcontrols2, shader tools, core5compat) | `src/apps/EpicMapEditor` |
| UI рантайма | Dear ImGui (через `util/sokol_imgui.h`) | `src/apps/EpicGameClient` |
| Тесты | gtest | `src/libs/tests` |

### CMake-макросы (настоящие)
- `nw_add_qml_app(NAME ... LIBS ...)` — для QML-приложений: добавляет QML-модуль
  (`qt6_add_qml_module`), общий include path на `src/libs`, подключает `pch.h`, на MSVC
  делает post-build деплой через `windeployqt`.
- `nw_add_lib_sources(...)` — для внутренних библиотек, далее линкуется в `LIBS` у приложений.
- Оба определены в `cmake/utils.cmake`.
- PCH: в каждой lib/app есть `pch.h`, подключается первым.

### Слой данных (без Qt) `[есть]`
- `src/libs/game_data` — чистые структуры + JSON load/save, не зависит от Qt.
  Заголовки: `assets.h` (`AssetIndex`), `map.h` (`Map`, `Layer`, `GameObject`), `types.h`
  (`LayerType`, `GameObjectType`).
- `src/libs/game_runtime` — `Runtime`, `RuntimeConfig`, `GameSession`, `GameWorld`, `Fixture`.
  Сейчас `GameWorld` хранит данные в `std::unordered_map` (персонажи/инвентари/квесты/время),
  **не** на ECS.

---

## Проверено прототипами, в продакшн не въехало `[прототип]`

Эти решения отработаны в песочницах и описаны ниже как готовые рецепты, но в основной
код (редактор/рантайм) ещё не перенесены. При переносе — сверять с актуальным кодом.

### ECS (EnTT)
- **В коде:** только `src/apps/EcsPlayground` и `RttrPlayground`.
- В рантайме `GameWorld` использует plain `std::unordered_map`; `entt::registry` в
  `game_world.h` закомментирован. То есть ECS — пока прототип, не архитектура ядра.

### Рефлексия (RTTR)
- **В коде:** только `RttrPlayground` (`Components.h` с `RTTR_ENABLE()`, регистрация в
  `Components.cpp`). Авто-инспектор/авто-сериализация в редакторе на RTTR не сделаны.

### ECS ↔ QAbstractListModel (адаптер)
- **В коде:** `src/apps/EcsPlayground/EcsModel.*`. В редакторе не используется
  (там живут отдельные модели вроде `core/models/chapters_model`).

### Sokol ↔ QtQuick
- **В коде (продакшн-редактор):** `src/apps/EpicMapEditor/src/map_render_item.*` —
  `MapRenderItem : QQuickFramebufferObject`, рендерит карту общим `WorldRenderer`
  в FBO; мост Qt-моделей — `src/apps/EpicMapEditor/src/map_frame_bridge.*`.
  Qt Quick форсирован в OpenGL (`main.cpp`), sokol — GLCORE с runtime-выбором
  шейдеров по `sg_query_backend()` в `render_core`.
- **В коде (прототип):** `src/apps/EcsPlayground/GameView.*` — там же боевые
  правила интеграции (см. ниже); GL-клей живёт в `EcsPlayground/graphics/`.
- **Удалено:** `RuntimeMapView` (недоподключенная заготовка через
  `beforeRendering`/`afterRendering`), QSG-версии `DiamondGrid`/`DiamondCursor`,
  Mandelbrot-`CustomItem` — заменены `MapRenderItem`. Водный фон старого
  `CustomItem` (каустики) восстановлен в новом пути: `src/water_background.*`
  (world-space квад 20000×20000 перед миром, iTime-анимация).

---

## Запланировано / целевые имена `[концепт]`

Эти имена встречаются в документах/обсуждениях как целевая архитектура. В коде их нет,
функциональность (если есть) разложена иначе:

- **`graphics_core` / `graphics_shell_qtquick` / `graphics_shell_sokolapp`** — целевые либы
  с общим рендер-ядром и двумя шеллами. Сейчас: общий код в `src/libs/render_core`,
  шеллы — inline в приложениях. (Бывшая либа `src/libs/graphics` удалена: её GL-клей
  переехал в `EcsPlayground` — единственного потребителя.)
- **`editor_qt_adapters`** — целевой target для Qt-экспорта enum'ов и view-models. Сейчас
  Qt-модели живут прямо в `src/libs/core`.
- **`IProfileStore` / `IInventoryStore` / `ISaveGameStore`** — целевые интерфейсы «хранилищ»
  для плейтеста. Сейчас подход другой — через `game_runtime::Fixture`.
- **immer** — undo/redo на immutable snapshots. В `src/` нет ни одного включения.
- **Lua/Sol2** — скриптинг квестов/диалогов. В `src/` нет.
- **simdjson/yyjson** — резервная замена nlohmann при проблемах с перфом.
- **Timeline Editor**, **Node Graph (quests/dialogs)** — инструментов в репозитории нет.

---

## Боевая кухня: интеграция Sokol с QtQuick (из прототипа)

Ниже — правила, выработанные в `src/apps/EcsPlayground/GameView.cpp` и дополненные
продакшн-путём `src/apps/EpicMapEditor/src/map_render_item.cpp` (тот же FBO-подход).

- **Принудительный графический API:** для совместимости Sokol + QtQuick требуется OpenGL:
  `QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL)`.
- **Инициализация строго при активном контексте:** `sg_setup()` вызывать только когда
  GL-контекст активен (lazy-init в render-thread).
- **GL state «грязный»:** перед рисованием сбрасывать кеш состояний Sokol —
  `sg_reset_state_cache()` (Qt меняет GL-state между кадрами).
- **Рендер в FBO (для пути QQuickFramebufferObject):** текстуру
  `QOpenGLFramebufferObject` оборачивать в `sg_image` (`gl_texture_target = GL_TEXTURE_2D`),
  создавать `sg_view` для color attachment и передавать в `sg_pass.attachments`.
- **Зеркалирование по Y:** sokol рисует в FBO-текстуру с GL-ориентацией (row 0 = низ),
  Qt Quick показывает её без флипа → картинка перевёрнута относительно sokol_app.
  Ставить `setMirrorVertically(true)` на item (проверено скриншот-сравнением с клиентом).
- **High-DPI:** FBO физический (item size × devicePixelRatio), а QML-мышь/камера —
  логические. Рендерить в ЛОГИЧЕСКИХ координатах (view size = `item->width()/height()`,
  не `fbo->size()`) — масштабирование на физику делает Qt при показе. Иначе мышь и
  рендер «плывут» друг относительно друга пропорционально DPR (был баг: курсор сетки
  смещён от курсора мыши на 125%-дисплее).
- **Чужие GL-ошибки:** контекст общий с Qt, `glGetError` глобален: ошибка из Qt-фазы
  всплывает на следующем `_SG_GL_CHECK_ERROR` (debug-assert) — падало при открытии
  вкладки главы. Дренировать `glGetError()` в начале своего кадра (после
  `sg_reset_state_cache`); свои ошибки sokol ловит в том же кадре. То же и на
  шаге `sg_setup()`: lazy-init тоже обязан дренировать ДО вызова — первый кадровый
  дренаж успевает только после setup (macOS, падение на первом открытии вкладки).
- **Core profile на macOS:** Qt Quick по умолчанию просит legacy OpenGL 2.1
  контекст, а sokol GLCORE требует core profile. Задать до создания
  `QApplication`: `QSurfaceFormat` с `setVersion(4, 1)` + `CoreProfile` и
  `setDefaultFormat(...)` (4.1 — максимум у Apple; см. `main.cpp` редактора).
  В 2.1-контексте `GL_MAJOR_VERSION`/`GL_MINOR_VERSION` дают `GL_INVALID_ENUM`,
  что и отравляет `glGetError` перед ассертом `_sg_gl_init_limits`.
- **Depth/stencil:** если depth не оборачиваем в pass, в pipeline отключать ожидание depth:
  `pip_desc.depth.pixel_format = SG_PIXELFORMAT_NONE`.
- **Коммит кадра:** `sg_commit()` ровно один раз на кадр на верхнем уровне рендера
  (после `sg_end_pass()`). Не прятать `sg_commit()` внутри общих `end_frame()`, если есть
  offscreen pass.
- **Shutdown при выходе из приложения:** `sg_shutdown()` НЕ вызывать вообще —
  `aboutToQuit` и деструкторы идут на GUI-потоке, где GL-контекст не актуален,
  а sokol при destroy ресурсов требует контекст (доказано cdb: падение при
  закрытии, `wassert` в `_sg_gl_discard_buffer` ← `sg_shutdown`). Процесс
  выходит — драйвер сам заберёт ресурсы при смерти контекста. Вместо shutdown:
  атомарный флаг `sokolValid=false`, который (а) останавливает `render()`
  (early-out), (б) запрещает деструкторам рендереров любые `sg_destroy_*`.
  Закрытие вкладки/view в РАНТАЙМЕ — другое дело: там деструктор на render-потоке
  с живым контекстом и обязан чистить свои sg-ресурсы (по тому же флагу).
- **Сброс GL-состояния для Qt:** после Sokol-рендера возвращать критичные состояния
  (unbind program/buffers, отключить depth/cull), чтобы не ломать последующий рендер Qt.

## Боевая кухня: ECS ↔ QAbstractListModel (из прототипа)

Из `src/apps/EcsPlayground`. Применимо, когда редактор переедет на EnTT.

- `entt::registry` хранит только данные; UI читает их через Qt-модель (адаптер), не напрямую.
- Для стабильных индексов QML — кеш активных сущностей `std::vector<entt::entity>`
  (индекс в модели → `entt::entity`).
- Корректная реактивность QML через сигналы:
  - добавление: `beginInsertRows()` / `endInsertRows()`;
  - массовые изменения/очистка: `beginResetModel()` / `endResetModel()`;
  - точечные изменения: `dataChanged(first, last, {roles...})` — только затронутые строки/роли.
- Компоненты в UI выдавать как роли (`roleNames()`), а не через ручные геттеры на каждый
  компонент.

## Боевая кухня: RTTR Inspector для QML (из прототипа)

Из `src/apps/RttrPlayground`. Применимо при появлении авто-инспектора в редакторе.

- **Формат данных для QML:** собирать `QVariantList`, где каждый элемент — `QVariantMap`
  компонента: `{"name": <ComponentName>, "properties": [ {"name","typeName","value"}, ... ]}`.
- **Сборка инспектора:** итерация по известному набору компонент-типов, для каждого:
  `rttr::type::get<ComponentType>().get_properties()` + `prop.get_value(rttr::instance(comp))`.
- **Редактирование** `setProperty(row, compName, propName, QVariant)`:
  найти `rttr::type` по имени компонента → `get_property(propName)` → конвертировать
  `QVariant` в `rttr::variant` по `prop.get_type()` → `prop.set_value(...)`.
  После успешной записи обязательно эмитить `dataChanged()` для затронутых ролей.
- **Компоненты:** простые структуры с `RTTR_ENABLE()`; регистрация в отдельном `.cpp`
  через `RTTR_REGISTRATION`; инспекция через `rttr::type::get<Component>()`.
- **TODO:** унифицировать конверсию типов (bool/enum/пользовательские), добавить
  валидацию/нормализацию значений и сообщения об ошибках в UI.

---

## Логирование и диагностика `[есть]`
spdlog — стандартный логгер для приложений/библиотек (запуск, инспекция, ошибки,
состояние графической инициализации).

## Рендер-слой (цель) `[есть]`
Цель достигнута: один рендер мира, два shell/backend:
- **EpicGameClient (Standalone):** shell без Qt (`sokol_app`, D3D11) + debug UI на ImGui.
- **EpicMapEditor (Qt):** shell на QtQuick — `MapRenderItem` (QQuickFramebufferObject,
  GLCORE) + QML overlay (панели, тулзы, индикаторы). Карта рисуется тем же
  `WorldRenderer`, что в клиенте.

**Источники кадра `MapFrameSource` `[есть]`:** `MapRenderItem` не привязан к
моделям — мировые данные даёт втыкаемый источник (`buildWorldFrame`/
`ensureFrameAssets`/`tick`, `src/apps/EpicMapEditor/src/frame_source.h`):
- `ModelFrameSource` — редакторская вкладка (Qt-модели карты, живые правки);
- `RuntimeFrameSource` — игровая Play-Test вкладка (изолированный
  `game_runtime::Runtime` + сессия из `Fixture`, мок-профиль `newGame()`).
Сбор кадра из `game_data::Map` общий с клиентом — `render_core/world_frame_builder`.

**Общее ядро `[есть]`:** `src/libs/render_core` — `WorldRenderer` (фасад),
`LandscapeRenderer` (тайлы из атласов), `SpriteRenderer` (Tile2D), `OverlayRenderer`
(линии сетки/курсора). Вход — plain-данные (`WorldFrame`), без Qt и без game-типов.
Бэкенд выбирается в рантайме (`sg_query_backend()`): клиент — D3D11, редактор — GLCORE;
формат depth у пайплайнов — параметр `init()` (swapchain клиента — DEPTH_STENCIL,
FBO редактора — NONE).
Единая топология мира — `topology_core::DiamondIsometry` (No-Qt порт редакторской
математики); `StaggeredIsometry` остаётся только для playground'ов.

Оформление в виде отдельных либ `graphics_core` / `graphics_shell_qtquick` /
`graphics_shell_sokolapp` — не требуется, ядро живёт в `render_core`,
шеллы — inline в приложениях.

## Play-Test Host + Fixtures (Editor → Runtime) `[в работе]`
- Реализовано игровой вкладкой в редакторе (`TabType.Game`, `qml/GameTab.qml`):
  кнопка ▶ на канвасе (`PlayControl`) → `TabContentCreator.playChapter` → изолированный
  `game_runtime::Runtime` + сессия из `Fixture` (`newGame()`-мок). Одна вкладка на
  главу (дедуп по имени «▶ <глава>»), повторный Play = перезапуск (канал `extraData`
  с nonce → `RuntimeFrameSource.restart()`). Тик — из `synchronize()` рендер-итема
  (игра на паузе, когда вкладка скрыта). RPC-оп `play` для автоматизации.
- Текущий подход к данным — `game_runtime::Fixture` (декларативное описание начального
  состояния мира: карта, персонажи, инвентари, квесты, время). Билдер и
  (де)сериализация реализованы. Управление моками профиля — следующий шаг.
- Целевые интерфейсы `IProfileStore` / `IInventoryStore` / `ISaveGameStore` — пока концепт.

## Известные проблемы
- **Геометрия окна редактора на High-DPI:** кастомная рамка `EpicEditorWindow`
  (`src/libs/ui/main_window.cpp`) перехватывает `WM_NCCALCSIZE`/`WM_GETMINMAXINFO`,
  а `WM_SIZE` раньше глотался на смене maximize → `QWindow::geometry` и QML-контент
  застревали на декларированных 1920x1080 независимо от реального размера окна
  (правая часть уезжала за экран: `RightPanel`, правые оверлеи). Частично исправлено
  (WM_SIZE больше не глотается), но максимизация по-прежнему работает нестандартно —
  отдельная задача по переработке рамки окна.
