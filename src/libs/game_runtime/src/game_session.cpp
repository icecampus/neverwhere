#include "pch.h"
#include "game_runtime/game_session.h"
#include "game_runtime/runtime.h"

namespace game_runtime {

GameSession::GameSession(Runtime& runtime) : runtime_(runtime) {
    spdlog::debug("GameSession created");
}

GameSession::~GameSession() {
    spdlog::debug("GameSession destroyed");
}

void GameSession::initialize(const Fixture& fixture) {
    spdlog::info("Initializing game session with fixture: {}", fixture.name());
    
    // Очищаем текущее состояние
    world_.clear();
    
    // Применяем фикстуру
    fixture.apply(*this);
    
    state_ = SessionState::Running;
    sessionTime_ = 0.0f;
    frameCount_ = 0;
    
    spdlog::info("Game session initialized");
}

void GameSession::load(const std::filesystem::path& savePath) {
    spdlog::info("Loading game from: {}", savePath.string());
    
    try {
        std::ifstream file(savePath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open save file");
        }
        
        nlohmann::json json;
        file >> json;
        
        state_ = SessionState::Loading;
        
        // Загружаем состояние мира
        world_.clear();
        
        if (json.contains("world")) {
            const auto& worldJson = json["world"];
            
            if (worldJson.contains("time")) {
                const auto& time = worldJson["time"];
                world_.setTime(
                    time.value("day", 1),
                    time.value("hour", 8),
                    time.value("minute", 0)
                );
            }
            
            if (worldJson.contains("globalVars")) {
                for (const auto& [key, value] : worldJson["globalVars"].items()) {
                    world_.setGlobalVar(key, value);
                }
            }
            
            if (worldJson.contains("characters")) {
                for (const auto& charJson : worldJson["characters"]) {
                    world_.addCharacter(charJson.get<Character>());
                }
            }
            
            if (worldJson.contains("inventories")) {
                for (const auto& invJson : worldJson["inventories"]) {
                    world_.addInventory(invJson.get<Inventory>());
                }
            }
            
            if (worldJson.contains("quests")) {
                for (const auto& questJson : worldJson["quests"]) {
                    world_.addQuest(questJson.get<QuestProgress>());
                }
            }
        }
        
        if (json.contains("mapPath")) {
            world_.loadMap(json["mapPath"].get<std::string>());
        }
        
        state_ = SessionState::Running;
        spdlog::info("Game loaded successfully");
        
    } catch (const std::exception& e) {
        spdlog::error("Failed to load game: {}", e.what());
        state_ = SessionState::Terminated;
        throw;
    }
}

void GameSession::save(const std::filesystem::path& savePath) const {
    spdlog::info("Saving game to: {}", savePath.string());
    
    nlohmann::json json;
    json["version"] = 1;
    json["mapPath"] = world_.map() ? "" : "";
    
    // Сохраняем мир
    nlohmann::json worldJson;
    worldJson["time"] = {
        {"day", world_.getDay()},
        {"hour", world_.getHour()},
        {"minute", world_.getMinute()}
    };
    
    worldJson["characters"] = world_.getAllCharacters();
    worldJson["inventories"] = world_.getAllInventories();
    worldJson["quests"] = world_.getAllQuests();
    
    json["world"] = worldJson;
    json["sessionTime"] = sessionTime_;
    json["frameCount"] = frameCount_;
    
    std::ofstream file(savePath);
    if (!file.is_open()) {
        throw std::runtime_error("Cannot create save file");
    }
    file << json.dump(2);
    
    spdlog::info("Game saved successfully");
}

void GameSession::start() {
    if (state_ == SessionState::Initializing) {
        state_ = SessionState::Running;
        spdlog::info("Game session started");
    }
}

void GameSession::pause() {
    if (state_ == SessionState::Running) {
        state_ = SessionState::Paused;
        spdlog::debug("Game session paused");
    }
}

void GameSession::resume() {
    if (state_ == SessionState::Paused) {
        state_ = SessionState::Running;
        spdlog::debug("Game session resumed");
    }
}

void GameSession::stop() {
    state_ = SessionState::Terminated;
    spdlog::info("Game session stopped");
}

void GameSession::update(float deltaTime) {
    if (state_ != SessionState::Running) {
        return;
    }
    
    sessionTime_ += deltaTime;
    frameCount_++;
    
    // Обновление времени мира
    // Например, 1 минута реального времени = 1 час игрового времени
    static float timeAccumulator = 0.0f;
    timeAccumulator += deltaTime;
    if (timeAccumulator >= 1.0f) { // Каждую секунду
        timeAccumulator -= 1.0f;
        world_.advanceTime(1); // 1 игровая минута
    }
}

Fixture GameSession::capture() const {
    return Fixture::capture(*this);
}

} // namespace game_runtime
