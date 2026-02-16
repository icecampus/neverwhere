#pragma once

#include "pch.h"
#include "game_runtime/game_types.h"

namespace game_runtime {

// Forward declarations
class GameSession;
class Runtime;

/**
 * @brief Игровой мир - ECS контейнер всех игровых данных
 * 
 * Использует EnTT для хранения сущностей и компонентов.
 * Предоставляет доступ к карте, объектам, персонажам и т.д.
 */
class GameWorld {
public:
    GameWorld();
    ~GameWorld();

    // Удаляем копирование, разрешаем перемещение
    GameWorld(const GameWorld&) = delete;
    GameWorld& operator=(const GameWorld&) = delete;
    GameWorld(GameWorld&&) = default;
    GameWorld& operator=(GameWorld&&) = default;

    // Загрузка карты
    void loadMap(const std::filesystem::path& mapPath);
    void loadMap(const game_data::Map& map);
    
    // Доступ к карте
    const game_data::Map* map() const { return map_.get(); }
    
    // ECS доступ
    // entt::registry& registry() { return registry_; }
    // const entt::registry& registry() const { return registry_; }
    
    // Доступ к слоям карты
    const std::vector<game_data::GameObject>* getLayer(game_data::LayerType type) const;
    
    // Работа с персонажами
    void addCharacter(const Character& character);
    void removeCharacter(const std::string& id);
    std::optional<Character> getCharacter(const std::string& id) const;
    std::vector<Character> getAllCharacters() const;
    
    // Работа с инвентарем
    void addInventory(const Inventory& inventory);
    void removeInventory(const std::string& ownerId);
    std::optional<Inventory> getInventory(const std::string& ownerId) const;
    std::vector<Inventory> getAllInventories() const;
    
    // Работа с квестами
    void addQuest(const QuestProgress& quest);
    void updateQuest(const QuestProgress& quest);
    void removeQuest(const std::string& questId);
    std::optional<QuestProgress> getQuest(const std::string& questId) const;
    std::vector<QuestProgress> getAllQuests() const;
    std::vector<QuestProgress> getActiveQuests() const;
    
    // Глобальные переменные
    void setGlobalVar(const std::string& name, const nlohmann::json& value);
    nlohmann::json getGlobalVar(const std::string& name) const;
    bool hasGlobalVar(const std::string& name) const;
    void removeGlobalVar(const std::string& name);
    
    // Время в игре
    void setTime(int day, int hour, int minute);
    void advanceTime(int minutes);
    int getDay() const { return day_; }
    int getHour() const { return hour_; }
    int getMinute() const { return minute_; }
    
    // Сброс мира
    void clear();

private:
    // ECS registry
    // entt::registry registry_;
    
    // Карта
    std::unique_ptr<game_data::Map> map_;
    
    // Персонажи
    std::unordered_map<std::string, Character> characters_;
    
    // Инвентари (public для сериализации)
    std::unordered_map<std::string, Inventory> inventories_;
    
    // Квесты
    std::unordered_map<std::string, QuestProgress> quests_;
    
    // Глобальные переменные
    std::unordered_map<std::string, nlohmann::json> globalVars_;
    
    // Время
    int day_ = 1;
    int hour_ = 8;
    int minute_ = 0;
};

} // namespace game_runtime
