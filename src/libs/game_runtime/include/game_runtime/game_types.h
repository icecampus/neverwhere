#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <memory>
#include <optional>

#include <glm/glm.hpp>
#include <nlohmann/json.hpp>

#include "game_data/types.h"
#include "game_data/map.h"

namespace game_runtime {

// Forward declarations
class GameSession;
class GameWorld;
class IRuntimeExtension;

/**
 * @brief Игровой предмет в инвентаре
 */
struct InventoryItem {
    std::string itemId;
    std::string instanceId;
    int quantity = 1;
    std::unordered_map<std::string, nlohmann::json> properties;
};

/**
 * @brief Инвентарь игрока или контейнера
 */
struct Inventory {
    std::string ownerId;
    std::vector<InventoryItem> items;
    int maxSlots = 20;
};

/**
 * @brief Состояние квеста
 */
enum class QuestStatus : int {
    NotStarted = 0,
    Active = 1,
    Completed = 2,
    Failed = 3
};

/**
 * @brief Прогресс квеста
 */
struct QuestProgress {
    std::string questId;
    QuestStatus status = QuestStatus::NotStarted;
    int currentStage = 0;
    std::unordered_map<std::string, int> objectives;
    std::unordered_map<std::string, nlohmann::json> customData;
};

/**
 * @brief Игровой персонаж (NPC или игрок)
 */
struct Character {
    std::string id;
    std::string name;
    std::string archetypeId;
    glm::ivec2 position{0, 0};
    std::unordered_map<std::string, int> attributes;
    std::unordered_map<std::string, nlohmann::json> state;
};

/**
 * @brief Состояние игрового мира
 */
struct WorldState {
    std::string worldId;
    int day = 1;
    int hour = 8;
    int minute = 0;
    std::unordered_map<std::string, nlohmann::json> globalVariables;
    std::unordered_map<std::string, nlohmann::json> unlockedContent;
};

} // namespace game_runtime

// JSON serialization functions - placed in nlohmann namespace for ADL
namespace nlohmann {
    template<>
    struct adl_serializer<glm::ivec2> {
        static void to_json(json& j, const glm::ivec2& pos) {
            j = json{{"x", pos.x}, {"y", pos.y}};
        }
        static void from_json(const json& j, glm::ivec2& pos) {
            j.at("x").get_to(pos.x);
            j.at("y").get_to(pos.y);
        }
    };

    template<>
    struct adl_serializer<game_runtime::InventoryItem> {
        static void to_json(json& j, const game_runtime::InventoryItem& item) {
            j = json{
                {"itemId", item.itemId},
                {"instanceId", item.instanceId},
                {"quantity", item.quantity},
                {"properties", item.properties}
            };
        }
        static void from_json(const json& j, game_runtime::InventoryItem& item) {
            j.at("itemId").get_to(item.itemId);
            j.at("instanceId").get_to(item.instanceId);
            j.at("quantity").get_to(item.quantity);
            j.at("properties").get_to(item.properties);
        }
    };

    template<>
    struct adl_serializer<game_runtime::Inventory> {
        static void to_json(json& j, const game_runtime::Inventory& inv) {
            j = json{
                {"ownerId", inv.ownerId},
                {"items", inv.items},
                {"maxSlots", inv.maxSlots}
            };
        }
        static void from_json(const json& j, game_runtime::Inventory& inv) {
            j.at("ownerId").get_to(inv.ownerId);
            j.at("items").get_to(inv.items);
            j.at("maxSlots").get_to(inv.maxSlots);
        }
    };

    template<>
    struct adl_serializer<game_runtime::QuestStatus> {
        static void to_json(json& j, const game_runtime::QuestStatus& status) {
            j = static_cast<int>(status);
        }
        static void from_json(const json& j, game_runtime::QuestStatus& status) {
            status = static_cast<game_runtime::QuestStatus>(j.get<int>());
        }
    };

    template<>
    struct adl_serializer<game_runtime::QuestProgress> {
        static void to_json(json& j, const game_runtime::QuestProgress& quest) {
            j = json{
                {"questId", quest.questId},
                {"status", quest.status},
                {"currentStage", quest.currentStage},
                {"objectives", quest.objectives},
                {"customData", quest.customData}
            };
        }
        static void from_json(const json& j, game_runtime::QuestProgress& quest) {
            j.at("questId").get_to(quest.questId);
            j.at("status").get_to(quest.status);
            j.at("currentStage").get_to(quest.currentStage);
            j.at("objectives").get_to(quest.objectives);
            j.at("customData").get_to(quest.customData);
        }
    };

    template<>
    struct adl_serializer<game_runtime::Character> {
        static void to_json(json& j, const game_runtime::Character& character) {
            j = json{
                {"id", character.id},
                {"name", character.name},
                {"archetypeId", character.archetypeId},
                {"position", character.position},
                {"attributes", character.attributes},
                {"state", character.state}
            };
        }
        static void from_json(const json& j, game_runtime::Character& character) {
            j.at("id").get_to(character.id);
            j.at("name").get_to(character.name);
            j.at("archetypeId").get_to(character.archetypeId);
            j.at("position").get_to(character.position);
            j.at("attributes").get_to(character.attributes);
            j.at("state").get_to(character.state);
        }
    };

    template<>
    struct adl_serializer<game_runtime::WorldState> {
        static void to_json(json& j, const game_runtime::WorldState& state) {
            j = json{
                {"worldId", state.worldId},
                {"day", state.day},
                {"hour", state.hour},
                {"minute", state.minute},
                {"globalVariables", state.globalVariables},
                {"unlockedContent", state.unlockedContent}
            };
        }
        static void from_json(const json& j, game_runtime::WorldState& state) {
            j.at("worldId").get_to(state.worldId);
            j.at("day").get_to(state.day);
            j.at("hour").get_to(state.hour);
            j.at("minute").get_to(state.minute);
            j.at("globalVariables").get_to(state.globalVariables);
            j.at("unlockedContent").get_to(state.unlockedContent);
        }
    };
}
