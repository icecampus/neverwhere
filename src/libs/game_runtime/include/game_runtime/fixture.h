#pragma once

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <optional>

#include <nlohmann/json.hpp>

#include "game_runtime/game_types.h"

namespace game_runtime {

// Forward declarations
class GameSession;

/**
 * @brief Фикстура - декларативное описание состояния игрового мира
 * 
 * Аналог фикстур в Python тестах (pytest).
 * Позволяет описать начальное состояние игры для тестирования,
 * редактирования или воспроизведения сценариев.
 */
class Fixture {
public:
    struct Builder;
    
    Fixture() = default;
    
    // Конструктор из JSON
    static Fixture fromJson(const nlohmann::json& json);
    static Fixture fromFile(const std::string& path);
    
    // Сериализация
    nlohmann::json toJson() const;
    void saveToFile(const std::string& path) const;
    
    // Применение фикстуры к сессии
    void apply(GameSession& session) const;
    
    // Создание фикстуры из текущего состояния сессии
    static Fixture capture(const GameSession& session);
    
    // Билдер для удобного создания фикстур
    static Builder create();
    
    // Доступ к данным
    const std::optional<WorldState>& worldState() const { return worldState_; }
    const std::vector<Character>& characters() const { return characters_; }
    const std::vector<Inventory>& inventories() const { return inventories_; }
    const std::vector<QuestProgress>& quests() const { return quests_; }
    const std::optional<std::string>& mapPath() const { return mapPath_; }
    const std::optional<std::string>& assetsRoot() const { return assetsRoot_; }
    const std::string& name() const { return name_; }
    const std::string& description() const { return description_; }
    
    // Модификация
    void setWorldState(const WorldState& state) { worldState_ = state; }
    void addCharacter(const Character& character) { characters_.push_back(character); }
    void addInventory(const Inventory& inventory) { inventories_.push_back(inventory); }
    void addQuest(const QuestProgress& quest) { quests_.push_back(quest); }
    void setMapPath(const std::string& path) { mapPath_ = path; }
    void setAssetsRoot(const std::string& root) { assetsRoot_ = root; }
    void setName(const std::string& name) { name_ = name; }
    void setDescription(const std::string& desc) { description_ = desc; }

private:
    std::string name_;
    std::string description_;
    
    // Игровое состояние
    std::optional<WorldState> worldState_;
    std::vector<Character> characters_;
    std::vector<Inventory> inventories_;
    std::vector<QuestProgress> quests_;
    
    // Контент
    std::optional<std::string> mapPath_;
    std::optional<std::string> assetsRoot_;
    
    // Дополнительные данные фикстуры
    nlohmann::json customData_;
};

/**
 * @brief Билдер для создания фикстур
 */
struct Fixture::Builder {
    Builder& withName(const std::string& name) {
        fixture_.setName(name);
        return *this;
    }
    
    Builder& withDescription(const std::string& desc) {
        fixture_.setDescription(desc);
        return *this;
    }
    
    Builder& withMap(const std::string& mapPath) {
        fixture_.setMapPath(mapPath);
        return *this;
    }
    
    Builder& withAssets(const std::string& assetsRoot) {
        fixture_.setAssetsRoot(assetsRoot);
        return *this;
    }
    
    Builder& withWorldState(const WorldState& state) {
        fixture_.setWorldState(state);
        return *this;
    }
    
    Builder& withCharacter(const Character& character) {
        fixture_.addCharacter(character);
        return *this;
    }
    
    Builder& withInventory(const Inventory& inventory) {
        fixture_.addInventory(inventory);
        return *this;
    }
    
    Builder& withQuest(const QuestProgress& quest) {
        fixture_.addQuest(quest);
        return *this;
    }
    
    // Готовые пресеты
    Builder& emptyWorld();
    Builder& newGame();
    Builder& debugScenario();
    
    Fixture build() { return std::move(fixture_); }
    
private:
    Fixture fixture_;
};

/**
 * @brief Реестр фикстур
 */
class FixtureRegistry {
public:
    static FixtureRegistry& instance();
    
    // Регистрация фикстур
    void registerFixture(const std::string& name, Fixture fixture);
    void registerFixture(const std::string& name, std::function<Fixture()> factory);
    
    // Получение фикстуры
    std::optional<Fixture> get(const std::string& name) const;
    
    // Список доступных фикстур
    std::vector<std::string> listFixtures() const;
    
    // Загрузка всех фикстур из директории
    void loadFromDirectory(const std::string& path);

private:
    FixtureRegistry() = default;
    
    std::unordered_map<std::string, Fixture> fixtures_;
    std::unordered_map<std::string, std::function<Fixture()>> factories_;
};

/**
 * @brief Макрос для удобной регистрации фикстур
 */
#define REGISTER_FIXTURE(name, fixture) \
    static struct FixtureRegistrator_##name { \
        FixtureRegistrator_##name() { \
            game_runtime::FixtureRegistry::instance().registerFixture(#name, fixture); \
        } \
    } fixtureRegistrator_##name;

} // namespace game_runtime
