# Neverwhere

Neverwhere — это **узкоспециализированный 2D engine/suite** под system-driven игры (life-sim и narrative casual). Репозиторий содержит **редактор** и **runtime-клиент**.

- **Epic Map Editor**: редактор игровых данных и контента
- **EpicGameRuntime**: игровой клиент (runtime) для запуска/проверки результата

## Apps
- **Editor**: `src/apps/EpicMapEditor`
- **Runtime (EpicGameRuntime)**: `src/apps/NeverwhereGame` *(текущее имя таргета/папки; переименование — отдельная задача)*

## High-level architecture

```mermaid
flowchart TB
  subgraph DataLayer[DataLayer_NoQt]
    gameData[game_data(JSON_Schema+LoadSave)]
    balanceData[balance_tables(JSON)]
    resourcesData[resources_manifest]
  end

  subgraph RenderLayer[RenderLayer_Shared]
    renderCore[graphics_core(Sokol_GFX_API)]
  end

  subgraph EditorApp[EpicMapEditor(Qt)]
    qmlUi[QML_UI]
    qtAdapters[editor_qt_adapters]
    qtShell[graphics_shell_qtquick]
    playtestHost[playtest_host]
  end

  subgraph RuntimeApp[EpicGameRuntime(Standalone)]
    imguiUi[ImGui_UI]
    runtimeShell[graphics_shell_sokolapp]
    gameLoop[game_loop]
  end

  gameData --> qtAdapters
  gameData --> gameLoop
  balanceData --> qtAdapters
  balanceData --> gameLoop
  resourcesData --> qtAdapters
  resourcesData --> gameLoop

  renderCore --> qtShell
  renderCore --> runtimeShell

  qtShell --> qmlUi
  runtimeShell --> imguiUi

  playtestHost -->|"launch_with_map+fixtures"| gameLoop
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
- **Runtime**: standalone shell (сейчас `sokol_app`) + игровой UI на **Dear ImGui**

## Play-Test в редакторе (концепт)
Редактор должен уметь **запускать EpicGameRuntime на текущей редактируемой карте**.

Если runtime-логике нужны данные, которых нет в карте (профиль игрока, прогресс, инвентарь, сейвы), редактор предоставляет их через **fixture-подобные провайдеры** (по аналогии с unit tests):\n- фиксированный “профиль по умолчанию”\n- тестовый инвентарь/прогресс\n- временное хранилище (in-memory) на время сессии плейтеста
Если runtime-логике нужны данные, которых нет в карте (профиль игрока, прогресс, инвентарь, сейвы), редактор предоставляет их через **fixture-подобные провайдеры** (по аналогии с unit tests):
- фиксированный “профиль по умолчанию”
- тестовый инвентарь/прогресс
- временное хранилище (in-memory) на время сессии плейтеста

## Build (Windows, CMake + vcpkg)
Сборка использует vcpkg (подключён как submodule). Мы **не патчим** `toolchain/vcpkg` напрямую.

Если нужен фикс портов (пример: совместимость `sokol_imgui.h` с ImGui 1.91+), используем **overlay ports**:
- `vcpkg_overlays/ports`
- передать в CMake: `-DVCPKG_OVERLAY_PORTS=<abs_path>/vcpkg_overlays/ports`

Пример (VS 2022):

```bash
cmake -S . -B "_intermediate_64" -DCMAKE_TOOLCHAIN_FILE=toolchain/vcpkg/scripts/buildsystems/vcpkg.cmake -DVCPKG_OVERLAY_PORTS="D:/campus/neverwhere/vcpkg_overlays/ports" -G "Visual Studio 17 2022" -A x64
cmake --build "_intermediate_64" --config Debug --target NeverwhereGame
```

## Roadmap (кратко)
- **Undo/Redo**: перейти на неизменяемые снэпшоты через `immer` (см. `TECHNICAL_STACK.md`)
- **Data/View separation**: отделить данные от QObject/QML представлений
