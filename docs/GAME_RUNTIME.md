# Game Runtime Library

Библиотека `game_runtime` предоставляет единый рантайм для запуска игры в различных режимах: standalone плеер, встроенный в редактор, и тестовый режим.

## Архитектура

```
┌─────────────────────────────────────────────────────────────┐
│                        Applications                          │
├─────────────────────────────────────────────────────────────┤
│  EpicGameRuntime (Standalone) │ EpicMapEditor (Qt/QML)      │
├─────────────────────────────────────────────────────────────┤
│                    game_runtime Library                      │
├────────────────────────┬────────────────────────────────────┤
│  Runtime               │  Extensions                        │
│  ├─ RuntimeConfig      │  ├─ IRuntimeExtension (interface)  │
│  ├─ GameSession        │  ├─ GridEditorExtension            │
│  ├─ GameWorld          │  ├─ SelectionEditorExtension       │
│  ├─ Fixture            │  └─ ToolsEditorExtension           │
│  └─ Game Types         │                                    │
├────────────────────────┴────────────────────────────────────┤
│                     Base Libraries                           │
│  game_data │ render_core │ topology_core │ ECS (EnTT)       │
└─────────────────────────────────────────────────────────────┘
```

## Основные компоненты

### 1. Runtime

Главный класс управления жизненным циклом игры.

```cpp
#include <game_runtime/lib.h>

// Создание рантайма для плеера
auto runtime = game_runtime::RuntimeFactory::createPlayer("path/to/map.json");
if (runtime.initialize()) {
    runtime.run();  // Блокирующий вызов главного цикла
}
```

### 2. Fixture (Фикстуры)

Декларативное описание начального состояния игрового мира.

```cpp
// Создание фикстуры через Builder
auto fixture = game_runtime::Fixture::create()
    .withName("Test Level")
    .withDescription("Level for testing combat")
    .withMap("resources/chapters/Base/maps/test_map.json")
    .withWorldState({
        .day = 1,
        .hour = 12,
        .minute = 0
    })
    .withCharacter({
        .id = "player",
        .name = "Hero",
        .position = {10, 10},
        .attributes = {{"health", 100}, {"mana", 50}}
    })
    .withQuest({
        .questId = "tutorial_01",
        .status = QuestStatus::Active,
        .currentStage = 1
    })
    .build();

// Сохранение в файл
fixture.saveToFile("fixtures/test_level.json");

// Загрузка из файла
auto loaded = game_runtime::Fixture::fromFile("fixtures/test_level.json");
```

### 3. GameSession

Управление игровой сессией с загрузкой/сохранением.

```cpp
// Создание сессии из фикстуры
auto* session = runtime.createSession(fixture);

// Управление состоянием
session->pause();
session->resume();
session->stop();

// Сохранение/загрузка
session->save("saves/slot1.json");
session->load("saves/slot1.json");

// Захват текущего состояния как фикстуры
auto currentState = session->capture();
```

### 4. GameWorld

Контейнер всех игровых данных.

```cpp
auto& world = session->world();

// Работа с персонажами
world.addCharacter({.id = "npc_1", .name = "Merchant"});
auto npc = world.getCharacter("npc_1");

// Работа с квестами
world.addQuest({.questId = "q1", .status = QuestStatus::Active});
auto activeQuests = world.getActiveQuests();

// Глобальные переменные
world.setGlobalVar("player_level", 5);
auto level = world.getGlobalVar("player_level");
```

### 5. Расширения редактора

Интерфейс для добавления функциональности редактирования.

```cpp
// Создание собственного расширения
class MyEditorTool : public game_runtime::EditorExtension {
public:
    const char* name() const override { return "MyTool"; }
    
    void update(Runtime& runtime, float deltaTime) override {
        // Логика инструмента
    }
    
    void renderUI(Runtime& runtime) override {
        // ImGui или Qt UI
    }
};

// Регистрация
runtime.registerExtension(std::make_unique<MyEditorTool>());
runtime.enableEditorMode();  // Включаем все расширения редактора
```

## Режимы использования

### 1. Standalone Player

```cpp
// src/apps/EpicGameRuntime/main.cpp (новая версия)
#include <game_runtime/lib.h>

int main(int argc, char* argv[]) {
    // Парсинг аргументов
    std::filesystem::path mapPath = argc > 1 ? argv[1] : "resources/maps/default.json";
    
    // Создание и запуск
    auto runtime = game_runtime::RuntimeFactory::createPlayer(mapPath);
    
    if (!runtime.initialize()) {
        return 1;
    }
    
    runtime.run();
    return 0;
}
```

### 2. Встроенный в редактор (Qt/QML)

```cpp
// В EpicMapEditor
#include <game_runtime/lib.h>

class EditorRuntimeWrapper : public QObject {
    Q_OBJECT
public:
    void initialize() {
        auto config = game_runtime::RuntimeConfig{};
        config.enableEditorExtensions = true;
        config.windowWidth = m_viewportWidth;
        config.windowHeight = m_viewportHeight;
        
        m_runtime = std::make_unique<game_runtime::Runtime>(config);
        m_runtime->initialize();
        
        // Регистрируем расширения редактора
        m_runtime->registerExtension(std::make_unique<GridEditorExtension>());
        m_runtime->registerExtension(std::make_unique<SelectionEditorExtension>());
        m_runtime->registerExtension(std::make_unique<ToolsEditorExtension>());
    }
    
    // Вызывается из QSG render thread
    void render() {
        if (m_runtime) {
            m_runtime->render();
        }
    }
    
    // Вызывается из главного треда Qt
    void update(float dt) {
        if (m_runtime) {
            m_runtime->update(dt);
        }
    }

private:
    std::unique_ptr<game_runtime::Runtime> m_runtime;
};
```

### 3. Тестовый режим

```cpp
// Тест с фикстурой
TEST(GameLogicTest, PlayerHealth) {
    auto fixture = game_runtime::Fixture::create()
        .withCharacter({
            .id = "player",
            .attributes = {{"health", 100}}
        })
        .build();
    
    auto runtime = game_runtime::RuntimeFactory::createTest(fixture);
    runtime.initialize();
    
    auto& world = runtime.currentSession()->world();
    auto player = world.getCharacter("player");
    
    EXPECT_EQ(player->attributes["health"], 100);
}
```

## Формат Fixture JSON

```json
{
  "name": "Test Dungeon",
  "description": "Dungeon level for testing",
  "mapPath": "resources/chapters/Dungeon/maps/level1.json",
  "assetsRoot": "resources/assets",
  "worldState": {
    "worldId": "dungeon_test",
    "day": 1,
    "hour": 14,
    "minute": 30,
    "globalVariables": {
      "difficulty": "hard",
      "unlocked_doors": ["door_1", "door_2"]
    }
  },
  "characters": [
    {
      "id": "player",
      "name": "Test Hero",
      "archetypeId": "warrior",
      "position": {"x": 5, "y": 10},
      "attributes": {
        "health": 100,
        "mana": 50,
        "strength": 15
      },
      "state": {
        "is_crouching": false
      }
    }
  ],
  "inventories": [
    {
      "ownerId": "player",
      "maxSlots": 20,
      "items": [
        {
          "itemId": "sword_iron",
          "instanceId": "sword_001",
          "quantity": 1,
          "properties": {
            "durability": 85
          }
        }
      ]
    }
  ],
  "quests": [
    {
      "questId": "main_001",
      "status": 1,
      "currentStage": 2,
      "objectives": {
        "enemies_killed": 5,
        "items_collected": 3
      }
    }
  ]
}
```

## Интеграция с Render

Рантайм не занимается непосредственным рендерингом, но предоставляет данные для рендерера:

```cpp
// В рендерере
void render() {
    auto& world = runtime.currentSession()->world();
    
    // Получаем тайлы карты
    auto* landscapeLayer = world.getLayer(game_data::LayerType::BaseLandscape);
    if (landscapeLayer) {
        for (const auto& obj : *landscapeLayer) {
            renderTile(obj.position, obj.assetUuid, obj.landscapeData->tileIndex);
        }
    }
    
    // Рендерим персонажей
    for (const auto& character : world.getAllCharacters()) {
        renderCharacter(character.position, character.archetypeId);
    }
}
```

## Следующие шаги

1. **Интеграция с EpicGameRuntime**: Заменить текущую реализацию на использование game_runtime
2. **Интеграция с EpicMapEditor**: Добавить Runtime в QML контекст
3. **Расширение ECS**: Добавить EnTT для более гибкой системы компонентов
4. **Система событий**: Добавить EventBus для коммуникации между системами
5. **Сохранение/загрузка**: Улучшить сериализацию с поддержкой версионирования
