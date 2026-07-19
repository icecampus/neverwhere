# Neverwhere

Neverwhere — это **узкоспециализированный 2D engine/suite** под system-driven игры (life-sim и narrative casual). Репозиторий содержит **редактор** и **runtime-клиент**.

- **Epic Map Editor**: редактор игровых данных и контента
- **EpicGameClient**: игровой клиент для запуска/проверки результата

## Apps
- **Editor**: `src/apps/EpicMapEditor`
- **Client (EpicGameClient)**: `src/apps/EpicGameClient`

## High-level architecture

Проект разбит на **слои**. Нижние слои не зависят от верхних; оба приложения
(редактор и рантайм) работают с **одной и той же моделью данных** и **одним
рендер-ядром**, отличаются только UI-шеллом.

### Слои

- **Слой данных (без Qt).** Чистые структуры + JSON load/save. Должен быть
  читаем без Qt, чтобы рантайм не тянул Qt-зависимости.
- **Рендер-ядро (общее).** Sokol GFX + утилиты рендера. Одно на оба приложения.
- **Runtime-логика (общая).** Игровой мир, сессия, цикл обновления, fixtures.
  Используется и в рантайме, и при плейтесте из редактора.
- **Приложения (шеллы).** Два разных «кожуха» поверх общих слоёв:
  - **EpicMapEditor** — Qt/QML. Рендер встроен в QtQuick через
    `QQuickFramebufferObject`/`QQuickItem` + OpenGL.
  - **EpicGameClient** — standalone на `sokol_app`, UI на Dear ImGui.

### Что уже есть в коде, а что пока концепт

Пометки ниже относятся к именам на диаграмме:

| Блок на диаграмме | Статус | Где реально живёт |
|---|---|---|
| `game_data` | есть | `src/libs/game_data` (`assets.h`, `map.h`, `types.h`) |
| `resources_manifest` | частично | есть `AssetIndex` (загрузка индекса ассетов), но как отдельный «манифест» не выделен |
| `balance_tables` | концепт | в коде пока нет |
| `render core` (Sokol) | есть | `src/libs/render_core` (`WorldRenderer`, `LandscapeRenderer`, `SpriteRenderer`, `OverlayRenderer`) + legacy GL-клей в `src/libs/graphics` |
| `editor_qt_adapters` | частично | Qt-модели-адаптеры (`QAbstractListModel` поверх EnTT) реально есть в `src/libs/core/models/`, но модуля с таким именем нет |
| `graphics_shell_qtquick` | есть (по сути) | `RuntimeMapView` (`src/apps/EpicMapEditor/src/runtime_map_view.*`) — встраивает рендер в QML |
| `graphics_shell_sokolapp` | есть (по сути) | весь `src/apps/EpicGameClient/main.cpp` — это шелл на `sokol_app` |
| `QML_UI` | есть | `src/apps/EpicMapEditor/qml/`, `resources.qrc` |
| `ImGui_UI` | есть | `simgui` в `EpicGameClient/main.cpp` |
| `game_loop` / `Runtime` | есть | `src/libs/game_runtime` (`Runtime`, `GameSession`, `GameWorld`) |
| `fixtures` | есть | `game_runtime::Fixture` (`include/game_runtime/fixture.h`) |
| `playtest_host` | частично | `RuntimeMapView` уже создаёт `Runtime` + сессию внутри редактора, но отдельного модуля «playtest host» нет |

### Схема

```mermaid
flowchart TB
  subgraph Data["Слой данных (без Qt)"]
    direction LR
    gameData["game_data<br/>карты, ассеты, типы"]
    balanceData["balance_tables<br/>(концепт)"]
    resourcesData["resources_manifest<br/>(частично: AssetIndex)"]
  end

  subgraph Shared["Общие ядра"]
    direction LR
    renderCore["Рендер-ядро<br/>Sokol GFX + render_core"]
    runtime["Runtime-логика<br/>Runtime / Session / World / Fixture"]
  end

  subgraph Editor["EpicMapEditor (Qt)"]
    direction TB
    qmlUi["QML UI"]
    qtShell["QtQuick shell<br/>(RuntimeMapView)"]
  end

  subgraph RuntimeApp["EpicGameClient (standalone)"]
    direction TB
    imguiUi["ImGui UI"]
    sokolShell["sokol_app shell"]
  end

  gameData --> Shared
  balanceData --> Shared
  resourcesData --> Shared

  renderCore --> qtShell
  renderCore --> sokolShell
  runtime --> qtShell
  runtime --> sokolShell

  qtShell --> qmlUi
  sokolShell --> imguiUi

  qtShell -. "запуск плейтеста:<br/>карта + fixture" .-> runtime
```

## Data layer (без Qt)
**Игровые данные** живут в JSON и должны быть доступны игре без Qt-зависимостей:
- **chapters** (главы) и **maps** (карты)
- **balance tables** (таблицы баланса, “excel-like” данные) в JSON
- **resources** (ассеты/пакеты ресурсов и их метаданные)

Редактор поверх этого добавляет **Qt/QML адаптеры** и инструменты редактирования, но базовые структуры и сериализация должны оставаться “чистыми”.

## Rendering
Рендер мира общий, но **разные shell/backend**:
- **Editor**: Qt/QML overlay, рендер мира встраивается в Qt (QtQuick/FBO)
- **EpicGameClient**: standalone shell (`sokol_app`) + игровой UI на **Dear ImGui**

## Play-Test в редакторе (концепт)
Редактор должен уметь **запускать EpicGameClient на текущей редактируемой карте**.

Если runtime-логике нужны данные, которых нет в карте (профиль игрока, прогресс, инвентарь, сейвы), редактор предоставляет их через **fixture-подобные провайдеры** (по аналогии с unit tests):
- фиксированный “профиль по умолчанию”
- тестовый инвентарь/прогресс
- временное хранилище (in-memory) на время сессии плейтеста

## Утилиты (`utils/`)
Вспомогательные Python-скрипты для генерации ассетов и проверки рендера. Подробности — в README каждой папки.

### Генерация атласов ландшафта — `utils/asset_generator/`
Стабильная техническая генерация тайлсета **4×6**, публикация в `resources/assets/landscape/TechnicalGrass*`. Зависимость: `Pillow`.
```bash
python utils/asset_generator/technical/generate_atlas_ridge.py --run-id <id>   # Ridge/Hybrid
python utils/asset_generator/technical/generate_atlas_valley.py --run-id <id>  # Valley/Concave
python utils/asset_generator/technical/generate_atlas_ridge.py --no-publish    # только temp, без записи в resources/
```
См. `utils/asset_generator/README.md` и `utils/asset_generator/technical/TILE_MASKS.md`.

### Blender-прототипы — `prototypes/blender/`
Исходные `.blend`-сцены ранних ручных прототипов геометрии (обрывы, hex-ландшафт) и референсы (`clifs/`, `landscape/hex/`, `reference/`). Используются вручную в Blender для визуальной проверки; в сборку и рантайм не входят. Каноничная логика ландшафта живёт в C++ (`src/libs/landscape_core`).

### Визуальные smoke-тесты — `utils/visual_tests/`
Запускает `MeshGenerationPlayground` со снятием скриншотов и гоняет OpenCV-эвристики по ним. Зависимости: см. `requirements.txt`.
```powershell
# предварительно собрать MeshGenerationPlayground
utils\visual_tests\run_visual_tests.ps1
```

## Разработка
- **Как работать с репозиторием** (сборка, конвенции, макросы, прототипы) — см. [`AGENTS.md`](./AGENTS.md).
- **Видение и статусы фич** — [`docs/ROADMAP_VISION.md`](./docs/ROADMAP_VISION.md).
- **Стек и архитектурные решения** — [`docs/TECHNICAL_STACK.md`](./docs/TECHNICAL_STACK.md).
