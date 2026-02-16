#include "pch.h"
#include "game_runtime/game_world.h"

namespace game_runtime {

GameWorld::GameWorld() = default;
GameWorld::~GameWorld() = default;

void GameWorld::loadMap(const std::filesystem::path& mapPath) {
    try {
        map_ = std::make_unique<game_data::Map>(game_data::Map::load(mapPath));
        spdlog::info("Loaded map from: {}", mapPath.string());
    } catch (const std::exception& e) {
        spdlog::error("Failed to load map {}: {}", mapPath.string(), e.what());
        throw;
    }
}

void GameWorld::loadMap(const game_data::Map& map) {
    map_ = std::make_unique<game_data::Map>(map);
}

const std::vector<game_data::GameObject>* GameWorld::getLayer(game_data::LayerType type) const {
    if (!map_) return nullptr;
    
    auto it = map_->layers.find(type);
    if (it != map_->layers.end()) {
        return &it->second;
    }
    return nullptr;
}

void GameWorld::addCharacter(const Character& character) {
    characters_[character.id] = character;
    spdlog::debug("Added character: {}", character.id);
}

void GameWorld::removeCharacter(const std::string& id) {
    characters_.erase(id);
    spdlog::debug("Removed character: {}", id);
}

std::optional<Character> GameWorld::getCharacter(const std::string& id) const {
    auto it = characters_.find(id);
    if (it != characters_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Character> GameWorld::getAllCharacters() const {
    std::vector<Character> result;
    result.reserve(characters_.size());
    for (const auto& [id, character] : characters_) {
        result.push_back(character);
    }
    return result;
}

void GameWorld::addInventory(const Inventory& inventory) {
    inventories_[inventory.ownerId] = inventory;
    spdlog::debug("Added inventory for: {}", inventory.ownerId);
}

void GameWorld::removeInventory(const std::string& ownerId) {
    inventories_.erase(ownerId);
    spdlog::debug("Removed inventory for: {}", ownerId);
}

std::optional<Inventory> GameWorld::getInventory(const std::string& ownerId) const {
    auto it = inventories_.find(ownerId);
    if (it != inventories_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<Inventory> GameWorld::getAllInventories() const {
    std::vector<Inventory> result;
    result.reserve(inventories_.size());
    for (const auto& [ownerId, inventory] : inventories_) {
        result.push_back(inventory);
    }
    return result;
}

void GameWorld::addQuest(const QuestProgress& quest) {
    quests_[quest.questId] = quest;
    spdlog::debug("Added quest: {} (status: {})", quest.questId, static_cast<int>(quest.status));
}

void GameWorld::updateQuest(const QuestProgress& quest) {
    quests_[quest.questId] = quest;
    spdlog::debug("Updated quest: {}", quest.questId);
}

void GameWorld::removeQuest(const std::string& questId) {
    quests_.erase(questId);
    spdlog::debug("Removed quest: {}", questId);
}

std::optional<QuestProgress> GameWorld::getQuest(const std::string& questId) const {
    auto it = quests_.find(questId);
    if (it != quests_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<QuestProgress> GameWorld::getAllQuests() const {
    std::vector<QuestProgress> result;
    result.reserve(quests_.size());
    for (const auto& [id, quest] : quests_) {
        result.push_back(quest);
    }
    return result;
}

std::vector<QuestProgress> GameWorld::getActiveQuests() const {
    std::vector<QuestProgress> result;
    for (const auto& [id, quest] : quests_) {
        if (quest.status == QuestStatus::Active) {
            result.push_back(quest);
        }
    }
    return result;
}

void GameWorld::setGlobalVar(const std::string& name, const nlohmann::json& value) {
    globalVars_[name] = value;
}

nlohmann::json GameWorld::getGlobalVar(const std::string& name) const {
    auto it = globalVars_.find(name);
    if (it != globalVars_.end()) {
        return it->second;
    }
    return nullptr;
}

bool GameWorld::hasGlobalVar(const std::string& name) const {
    return globalVars_.find(name) != globalVars_.end();
}

void GameWorld::removeGlobalVar(const std::string& name) {
    globalVars_.erase(name);
}

void GameWorld::setTime(int day, int hour, int minute) {
    day_ = day;
    hour_ = hour;
    minute_ = minute;
}

void GameWorld::advanceTime(int minutes) {
    minute_ += minutes;
    while (minute_ >= 60) {
        minute_ -= 60;
        hour_++;
    }
    while (hour_ >= 24) {
        hour_ -= 24;
        day_++;
    }
}

void GameWorld::clear() {
    map_.reset();
    characters_.clear();
    inventories_.clear();
    quests_.clear();
    globalVars_.clear();
    day_ = 1;
    hour_ = 8;
    minute_ = 0;
    spdlog::info("Game world cleared");
}

} // namespace game_runtime
