#pragma once

#include "pch.h"
#include "game_runtime/game_types.h"
#include "game_runtime/game_world.h"
#include "game_runtime/fixture.h"

namespace game_runtime {

// Forward declarations
class Runtime;
class IRuntimeExtension;

/**
 * @brief Состояние игровой сессии
 */
enum class SessionState {
    Initializing,
    Running,
    Paused,
    Saving,
    Loading,
    Terminated
};

/**
 * @brief Игровая сессия
 * 
 * Хранит текущее состояние игры, мир и управляет жизненным циклом.
 */
class GameSession {
public:
    explicit GameSession(Runtime& runtime);
    ~GameSession();

    // Удаляем копирование
    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    // Инициализация из фикстуры
    void initialize(const Fixture& fixture);
    
    // Загрузка/сохранение
    void load(const std::filesystem::path& savePath);
    void save(const std::filesystem::path& savePath) const;
    
    // Управление состоянием
    void start();
    void pause();
    void resume();
    void stop();
    
    // Обновление (вызывается каждый кадр)
    void update(float deltaTime);
    
    // Доступ к миру
    GameWorld& world() { return world_; }
    const GameWorld& world() const { return world_; }
    
    // Доступ к Runtime
    Runtime& runtime() { return runtime_; }
    const Runtime& runtime() const { return runtime_; }
    
    // Состояние
    SessionState state() const { return state_; }
    bool isRunning() const { return state_ == SessionState::Running; }
    bool isPaused() const { return state_ == SessionState::Paused; }
    
    // Время сессии
    float sessionTime() const { return sessionTime_; }
    int frameCount() const { return frameCount_; }
    
    // Создание фикстуры из текущего состояния
    Fixture capture() const;

private:
    Runtime& runtime_;
    GameWorld world_;
    SessionState state_ = SessionState::Initializing;
    
    float sessionTime_ = 0.0f;
    int frameCount_ = 0;
};

} // namespace game_runtime
