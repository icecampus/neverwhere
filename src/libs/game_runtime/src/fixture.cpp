#include "pch.h"
#include "game_runtime/fixture.h"
#include "game_runtime/game_session.h"
#include "game_runtime/game_world.h"

namespace game_runtime {

// ==================== Fixture Implementation ====================

Fixture Fixture::fromJson(const nlohmann::json& json) {
    Fixture fixture;
    
    if (json.contains("name")) {
        fixture.setName(json["name"].get<std::string>());
    }
    if (json.contains("description")) {
        fixture.setDescription(json["description"].get<std::string>());
    }
    if (json.contains("worldState")) {
        fixture.setWorldState(json["worldState"].get<WorldState>());
    }
    if (json.contains("characters") && json["characters"].is_array()) {
        for (const auto& charJson : json["characters"]) {
            fixture.addCharacter(charJson.get<Character>());
        }
    }
    if (json.contains("inventories") && json["inventories"].is_array()) {
        for (const auto& invJson : json["inventories"]) {
            fixture.addInventory(invJson.get<Inventory>());
        }
    }
    if (json.contains("quests") && json["quests"].is_array()) {
        for (const auto& questJson : json["quests"]) {
            fixture.addQuest(questJson.get<QuestProgress>());
        }
    }
    if (json.contains("mapPath")) {
        fixture.setMapPath(json["mapPath"].get<std::string>());
    }
    if (json.contains("assetsRoot")) {
        fixture.setAssetsRoot(json["assetsRoot"].get<std::string>());
    }
    
    return fixture;
}

Fixture Fixture::fromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open fixture file: " + path);
    }
    
    nlohmann::json json;
    file >> json;
    return fromJson(json);
}

nlohmann::json Fixture::toJson() const {
    nlohmann::json json;
    json["name"] = name_;
    json["description"] = description_;
    
    if (worldState_) {
        json["worldState"] = *worldState_;
    }
    if (!characters_.empty()) {
        json["characters"] = characters_;
    }
    if (!inventories_.empty()) {
        json["inventories"] = inventories_;
    }
    if (!quests_.empty()) {
        json["quests"] = quests_;
    }
    if (mapPath_) {
        json["mapPath"] = *mapPath_;
    }
    if (assetsRoot_) {
        json["assetsRoot"] = *assetsRoot_;
    }
    
    return json;
}

void Fixture::saveToFile(const std::string& path) const {
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to create fixture file: " + path);
    }
    file << toJson().dump(2);
}

void Fixture::apply(GameSession& session) const {
    auto& world = session.world();
    
    // Применяем мир
    if (worldState_) {
        world.setTime(worldState_->day, worldState_->hour, worldState_->minute);
        for (const auto& [key, value] : worldState_->globalVariables) {
            world.setGlobalVar(key, value);
        }
    }
    
    // Применяем персонажей
    for (const auto& character : characters_) {
        world.addCharacter(character);
    }
    
    // Применяем инвентари
    for (const auto& inventory : inventories_) {
        world.addInventory(inventory);
    }
    
    // Применяем квесты
    for (const auto& quest : quests_) {
        world.addQuest(quest);
    }
    
    // Загружаем карту
    if (mapPath_) {
        world.loadMap(*mapPath_);
    }
}

Fixture Fixture::capture(const GameSession& session) {
    Fixture fixture;
    const auto& world = session.world();
    
    // Захватываем состояние мира
    WorldState state;
    state.day = world.getDay();
    state.hour = world.getHour();
    state.minute = world.getMinute();
    state.worldId = "captured_" + std::to_string(session.frameCount());
    fixture.setWorldState(state);
    
    // Захватываем персонажей
    for (const auto& character : world.getAllCharacters()) {
        fixture.addCharacter(character);
    }
    
    // Захватываем инвентари
    // Note: нужно итерировать по известным владельцам
    
    // Захватываем квесты
    for (const auto& quest : world.getAllQuests()) {
        fixture.addQuest(quest);
    }
    
    return fixture;
}

Fixture::Builder Fixture::create() {
    return Builder{};
}

// Builder методы
Fixture::Builder& Fixture::Builder::emptyWorld() {
    fixture_.setName("empty_world");
    fixture_.setDescription("Empty world for testing");
    return *this;
}

Fixture::Builder& Fixture::Builder::newGame() {
    fixture_.setName("new_game");
    fixture_.setDescription("New game start");
    
    WorldState state;
    state.day = 1;
    state.hour = 8;
    state.minute = 0;
    fixture_.setWorldState(state);
    
    // Добавляем начальный инвентарь игрока
    Inventory playerInv;
    playerInv.ownerId = "player";
    playerInv.maxSlots = 20;
    fixture_.addInventory(playerInv);
    
    return *this;
}

Fixture::Builder& Fixture::Builder::debugScenario() {
    fixture_.setName("debug_scenario");
    fixture_.setDescription("Debug scenario with test data");
    
    WorldState state;
    state.day = 5;
    state.hour = 14;
    state.minute = 30;
    state.globalVariables["test_var"] = 42;
    state.globalVariables["player_name"] = "TestPlayer";
    fixture_.setWorldState(state);
    
    // Тестовый персонаж
    Character player;
    player.id = "player";
    player.name = "Test Player";
    player.archetypeId = "human_villager";
    player.position = glm::ivec2(10, 10);
    player.attributes["health"] = 100;
    player.attributes["stamina"] = 50;
    fixture_.addCharacter(player);
    
    // Тестовый инвентарь
    Inventory inv;
    inv.ownerId = "player";
    inv.maxSlots = 20;
    InventoryItem item;
    item.itemId = "sword_iron";
    item.instanceId = "sword_001";
    item.quantity = 1;
    inv.items.push_back(item);
    fixture_.addInventory(inv);
    
    // Тестовый квест
    QuestProgress quest;
    quest.questId = "tutorial_001";
    quest.status = QuestStatus::Active;
    quest.currentStage = 1;
    quest.objectives["talk_to_npc"] = 1;
    fixture_.addQuest(quest);
    
    return *this;
}

// ==================== FixtureRegistry Implementation ====================

FixtureRegistry& FixtureRegistry::instance() {
    static FixtureRegistry registry;
    return registry;
}

void FixtureRegistry::registerFixture(const std::string& name, Fixture fixture) {
    fixtures_[name] = std::move(fixture);
}

void FixtureRegistry::registerFixture(const std::string& name, std::function<Fixture()> factory) {
    factories_[name] = std::move(factory);
}

std::optional<Fixture> FixtureRegistry::get(const std::string& name) const {
    auto it = fixtures_.find(name);
    if (it != fixtures_.end()) {
        return it->second;
    }
    
    auto factoryIt = factories_.find(name);
    if (factoryIt != factories_.end()) {
        return factoryIt->second();
    }
    
    return std::nullopt;
}

std::vector<std::string> FixtureRegistry::listFixtures() const {
    std::vector<std::string> result;
    result.reserve(fixtures_.size() + factories_.size());
    
    for (const auto& [name, _] : fixtures_) {
        result.push_back(name);
    }
    for (const auto& [name, _] : factories_) {
        if (fixtures_.find(name) == fixtures_.end()) {
            result.push_back(name);
        }
    }
    
    return result;
}

void FixtureRegistry::loadFromDirectory(const std::string& path) {
    namespace fs = std::filesystem;
    
    if (!fs::exists(path) || !fs::is_directory(path)) {
        spdlog::warn("Fixture directory not found: {}", path);
        return;
    }
    
    for (const auto& entry : fs::directory_iterator(path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            try {
                auto fixture = Fixture::fromFile(entry.path().string());
                std::string name = entry.path().stem().string();
                registerFixture(name, std::move(fixture));
                spdlog::info("Loaded fixture: {}", name);
            } catch (const std::exception& e) {
                spdlog::error("Failed to load fixture {}: {}", entry.path().string(), e.what());
            }
        }
    }
}

// Регистрация стандартных фикстур
namespace {
    REGISTER_FIXTURE(empty_world, Fixture::create().emptyWorld().build())
    REGISTER_FIXTURE(new_game, Fixture::create().newGame().build())
    REGISTER_FIXTURE(debug_scenario, Fixture::create().debugScenario().build())
}

} // namespace game_runtime
