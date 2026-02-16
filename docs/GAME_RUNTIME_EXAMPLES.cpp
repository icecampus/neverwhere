#include <game_runtime/lib.h>
#include <iostream>

using namespace game_runtime;

// ============================================================================
// ПРИМЕР 1: Standalone плеер игры
// ============================================================================
void example_standalone_player() {
    // Создаем конфигурацию для плеера
    RuntimeConfig config;
    config.windowTitle = "My Game";
    config.defaultMap = "resources/chapters/Base/maps/map.json";
    config.assetsRoot = "resources/assets";
    config.enableEditorExtensions = false;  // Расширения редактора отключены
    
    // Создаем рантайм
    Runtime runtime(config);
    
    // Инициализация
    if (!runtime.initialize()) {
        std::cerr << "Failed to initialize runtime\n";
        return;
    }
    
    // Создаем сессию (будет загружена defaultMap)
    runtime.createSession();
    
    // Запускаем главный цикл (блокирующий)
    runtime.run();
}

// ============================================================================
// ПРИМЕР 2: Создание и использование фикстур
// ============================================================================
void example_fixtures() {
    // 1. Создание фикстуры через Builder
    auto fixture = Fixture::create()
        .withName("Combat Test")
        .withDescription("Test combat mechanics")
        .withMap("resources/maps/combat_arena.json")
        .withWorldState({
            .worldId = "test_combat",
            .day = 1,
            .hour = 12,
            .minute = 0,
            .globalVariables = {
                {"difficulty", "hard"},
                {"test_mode", true}
            }
        })
        .withCharacter({
            .id = "player",
            .name = "Test Hero",
            .archetypeId = "warrior",
            .position = {10, 10},
            .attributes = {
                {"health", 100},
                {"mana", 50},
                {"strength", 15},
                {"agility", 12}
            }
        })
        .withCharacter({
            .id = "enemy_1",
            .name = "Orc Warrior",
            .archetypeId = "orc_melee",
            .position = {15, 10},
            .attributes = {
                {"health", 80},
                {"strength", 12}
            }
        })
        .withInventory({
            .ownerId = "player",
            .maxSlots = 20,
            .items = {
                {
                    .itemId = "sword_iron",
                    .instanceId = "sword_001",
                    .quantity = 1,
                    .properties = {{"durability", 100}}
                },
                {
                    .itemId = "health_potion",
                    .instanceId = "potion_001",
                    .quantity = 5
                }
            }
        })
        .withQuest({
            .questId = "test_quest",
            .status = QuestStatus::Active,
            .currentStage = 1,
            .objectives = {
                {"enemies_defeated", 0},
                {"enemies_total", 1}
            }
        })
        .build();
    
    // 2. Сохранение фикстуры в файл
    fixture.saveToFile("fixtures/combat_test.json");
    
    // 3. Загрузка фикстуры из файла
    auto loadedFixture = Fixture::fromFile("fixtures/combat_test.json");
    
    // 4. Применение фикстуры к сессии
    Runtime runtime;
    runtime.initialize();
    runtime.applyFixture(loadedFixture);
    
    // 5. Захват текущего состояния как фикстуры
    auto currentState = runtime.captureCurrentState();
    currentState.saveToFile("fixtures/autosave.json");
}

// ============================================================================
// ПРИМЕР 3: Тестовый режим с фикстурой
// ============================================================================
void example_testing() {
    // Создаем фикстуру для теста
    auto testFixture = Fixture::create()
        .emptyWorld()  // Начинаем с пустого мира
        .withCharacter({
            .id = "test_npc",
            .name = "Test NPC",
            .position = {5, 5}
        })
        .build();
    
    // Создаем тестовый рантайм
    auto runtime = RuntimeFactory::createTest(testFixture);
    runtime.initialize();
    
    // Получаем доступ к миру
    auto& world = runtime.currentSession()->world();
    
    // Проверяем начальное состояние
    auto npc = world.getCharacter("test_npc");
    assert(npc.has_value());
    assert(npc->position == glm::ivec2(5, 5));
    
    // Модифицируем состояние
    world.setGlobalVar("test_passed", true);
    
    // Проверяем результат
    assert(world.hasGlobalVar("test_passed"));
    assert(world.getGlobalVar("test_passed").get<bool>() == true);
}

// ============================================================================
// ПРИМЕР 4: Расширения редактора
// ============================================================================

// Создаем собственное расширение
class CustomBrushTool : public EditorExtension {
public:
    const char* name() const override { return "CustomBrush"; }
    
    void onSessionCreated(GameSession& session) override {
        std::cout << "CustomBrush: Session created\n";
        selectedAsset_ = "grass_01";
    }
    
    void update(Runtime& runtime, float deltaTime) override {
        // Логика инструмента (обработка ввода и т.д.)
        if (isEditMode()) {
            // В режиме редактирования
        }
    }
    
    void renderUI(Runtime& runtime) override {
        // UI инструмента (ImGui или Qt)
        // ImGui::Text("Brush Tool");
        // ImGui::InputText("Asset", &selectedAsset_);
    }

private:
    std::string selectedAsset_;
    bool painting_ = false;
};

void example_editor_extensions() {
    RuntimeConfig config;
    config.enableEditorExtensions = true;  // Включаем расширения
    
    Runtime runtime(config);
    runtime.initialize();
    
    // Регистрируем стандартные расширения редактора
    runtime.registerExtension(std::make_unique<GridEditorExtension>());
    runtime.registerExtension(std::make_unique<SelectionEditorExtension>());
    runtime.registerExtension(std::make_unique<ToolsEditorExtension>());
    
    // Регистрируем собственное расширение
    runtime.registerExtension(std::make_unique<CustomBrushTool>());
    
    // Включаем/отключаем расширения
    runtime.enableExtension("GridEditor", true);
    runtime.enableExtension("CustomBrush", true);
    
    // Список доступных расширений
    auto extensions = runtime.getExtensionNames();
    for (const auto& name : extensions) {
        std::cout << "Extension: " << name << "\n";
    }
}

// ============================================================================
// ПРИМЕР 5: Работа с игровым миром
// ============================================================================
void example_game_world() {
    Runtime runtime;
    runtime.initialize();
    runtime.createSession();
    
    auto& world = runtime.currentSession()->world();
    
    // Добавление персонажей
    world.addCharacter({
        .id = "merchant",
        .name = "Bob the Merchant",
        .archetypeId = "human_merchant",
        .position = {20, 15},
        .attributes = {
            {"health", 50},
            {"gold", 1000}
        }
    });
    
    // Добавление квестов
    world.addQuest({
        .questId = "fetch_item",
        .status = QuestStatus::Active,
        .currentStage = 1,
        .objectives = {
            {"items_collected", 0},
            {"items_needed", 5}
        },
        .customData = {
            {"target_item", "herb_medicinal"},
            {"reward_gold", 100}
        }
    });
    
    // Обновление квеста
    auto quest = world.getQuest("fetch_item");
    if (quest) {
        quest->objectives["items_collected"] = 3;
        world.updateQuest(*quest);
    }
    
    // Получение активных квестов
    auto activeQuests = world.getActiveQuests();
    std::cout << "Active quests: " << activeQuests.size() << "\n";
    
    // Работа с глобальными переменными
    world.setGlobalVar("weather", "rainy");
    world.setGlobalVar("temperature", 15);
    
    auto weather = world.getGlobalVar("weather");
    std::cout << "Weather: " << weather.get<std::string>() << "\n";
    
    // Время в игре
    world.setTime(3, 14, 30);  // День 3, 14:30
    world.advanceTime(90);      // +90 минут = День 3, 16:00
    
    std::cout << "Day " << world.getDay() 
              << ", " << world.getHour() 
              << ":" << world.getMinute() << "\n";
}

// ============================================================================
// ПРИМЕР 6: Сохранение и загрузка
// ============================================================================
void example_save_load() {
    // Создаем сессию
    Runtime runtime;
    runtime.initialize();
    
    auto fixture = Fixture::create()
        .newGame()  // Начальное состояние новой игры
        .build();
    
    runtime.createSession(fixture);
    
    // Игровой процесс...
    auto& world = runtime.currentSession()->world();
    world.setGlobalVar("progress", 50);
    world.addQuest({.questId = "main", .status = QuestStatus::Active});
    
    // Сохранение
    runtime.currentSession()->save("saves/slot1.json");
    std::cout << "Game saved\n";
    
    // Загрузка в другую сессию
    Runtime runtime2;
    runtime2.initialize();
    runtime2.createSession();
    runtime2.currentSession()->load("saves/slot1.json");
    
    // Проверяем загруженные данные
    auto& world2 = runtime2.currentSession()->world();
    assert(world2.hasGlobalVar("progress"));
    assert(world2.getQuest("main").has_value());
}

// ============================================================================
// ПРИМЕР 7: Интеграция с рендерером
// ============================================================================
void example_render_integration() {
    Runtime runtime;
    runtime.initialize();
    runtime.createSession();
    
    // В цикле рендеринга:
    void render_frame() {
        auto* session = Runtime::current()->currentSession();
        if (!session) return;
        
        auto& world = session->world();
        
        // Рендеринг карты
        if (auto* layer = world.getLayer(game_data::LayerType::BaseLandscape)) {
            for (const auto& obj : *layer) {
                // renderTile(obj.position, obj.assetUuid, obj.landscapeData->tileIndex);
            }
        }
        
        // Рендеринг персонажей
        for (const auto& character : world.getAllCharacters()) {
            // renderCharacter(character.position, character.archetypeId);
        }
        
        // Рендеринг UI расширений редактора (если включены)
        // for (auto* ext : runtime.getEnabledExtensions()) {
        //     ext->renderUI(*Runtime::current());
        // }
    }
}

// ============================================================================
// Главная функция с демонстрацией всех примеров
// ============================================================================
int main() {
    std::cout << "Game Runtime Library Examples\n";
    std::cout << "=============================\n\n";
    
    // Раскомментируйте нужные примеры:
    
    // example_fixtures();
    // example_testing();
    // example_editor_extensions();
    // example_game_world();
    // example_save_load();
    // example_render_integration();
    
    // Для standalone плеера:
    // example_standalone_player();
    
    std::cout << "\nExamples completed!\n";
    return 0;
}
