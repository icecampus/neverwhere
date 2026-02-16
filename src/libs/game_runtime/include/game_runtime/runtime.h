#pragma once

#include "pch.h"
#include "game_runtime/game_types.h"
#include "game_runtime/game_session.h"
#include "game_runtime/game_world.h"
#include "game_runtime/fixture.h"
#include "game_runtime/editor_extensions.h"

namespace game_runtime {

// Forward declarations
class IRenderer;

/**
 * @brief Конфигурация Runtime
 */
struct RuntimeConfig {
    // Пути
    std::filesystem::path assetsRoot = "resources/assets";
    std::filesystem::path dataRoot;  // Автоопределение если пустой
    std::filesystem::path defaultMap = "resources/chapters/Base/maps/map.json";
    
    // Окно
    std::string windowTitle = "Game Runtime";
    int windowWidth = 1280;
    int windowHeight = 720;
    bool highDpi = true;
    bool fullscreen = false;
    bool vsync = true;
    
    // Игровые параметры
    float targetFps = 60.0f;
    bool fixedTimeStep = false;
    float fixedDeltaTime = 1.0f / 60.0f;
    
    // Расширения редактора (включаются только в редакторе)
    bool enableEditorExtensions = false;
    std::vector<std::string> editorExtensionNames;
    
    // Начальная фикстура
    std::optional<Fixture> initialFixture;
    
    // Коллбэки
    std::function<void(Runtime&)> onInitialized;
    std::function<void(Runtime&)> onShutdown;
};

/**
 * @brief Главный класс игрового рантайма
 * 
 * Управляет жизненным циклом игры, сессиями, расширениями и рендерингом.
 * Может использоваться как в standalone приложении, так и в редакторе.
 */
class Runtime {
public:
    explicit Runtime(const RuntimeConfig& config = {});
    ~Runtime();

    // Удаляем копирование, разрешаем перемещение
    Runtime(const Runtime&) = delete;
    Runtime& operator=(const Runtime&) = delete;
    Runtime(Runtime&&) = default;
    Runtime& operator=(Runtime&&) = default;

    // Инициализация и деинициализация
    bool initialize();
    void shutdown();
    bool isInitialized() const { return initialized_; }

    // Главный цикл (для standalone режима)
    void run();
    
    // Пошаговое управление (для интеграции в редактор)
    void update(float deltaTime);
    void render();
    void processEvents();
    bool shouldExit() const { return shouldExit_; }
    void requestExit() { shouldExit_ = true; }

    // Управление сессиями
    GameSession* createSession(const Fixture& fixture);
    GameSession* createSession();
    void destroySession(GameSession* session);
    GameSession* currentSession() { return currentSession_.get(); }
    const GameSession* currentSession() const { return currentSession_.get(); }

    // Фикстуры
    void applyFixture(const Fixture& fixture);
    Fixture captureCurrentState() const;

    // Расширения
    void registerExtension(std::unique_ptr<IRuntimeExtension> extension);
    void registerExtensionFactory(const std::string& name, ExtensionFactory factory);
    IRuntimeExtension* getExtension(const std::string& name);
    void enableExtension(const std::string& name, bool enable);
    std::vector<std::string> getExtensionNames() const;
    
    // Специальные методы для редактора
    void enableEditorMode();
    void disableEditorMode();
    bool isEditorMode() const { return config_.enableEditorExtensions; }

    // Доступ к конфигурации
    const RuntimeConfig& config() const { return config_; }
    RuntimeConfig& config() { return config_; }

    // Доступ к рендереру
    // IRenderer* renderer() { return renderer_.get(); }
    // const IRenderer* renderer() const { return renderer_.get(); }

    // Время
    float deltaTime() const { return deltaTime_; }
    float totalTime() const { return totalTime_; }
    int frameCount() const { return frameCount_; }

    // Статический доступ к текущему рантайму (для коллбэков)
    static Runtime* current() { return currentRuntime_; }

private:
    RuntimeConfig config_;
    bool initialized_ = false;
    bool shouldExit_ = false;
    
    // Сессия
    std::unique_ptr<GameSession> currentSession_;
    
    // Расширения
    std::unordered_map<std::string, std::unique_ptr<IRuntimeExtension>> extensions_;
    std::unordered_map<std::string, ExtensionFactory> extensionFactories_;
    std::vector<IRuntimeExtension*> enabledExtensions_;
    
    // Рендерер
    // std::unique_ptr<IRenderer> renderer_;
    
    // Время
    float deltaTime_ = 0.0f;
    float totalTime_ = 0.0f;
    int frameCount_ = 0;
    
    // Статический указатель на текущий рантайм
    static Runtime* currentRuntime_;
    
    // Инициализация расширений
    void initializeExtensions();
    void shutdownExtensions();
    
    // Обновление расширений
    void updateExtensions(float deltaTime);
    void lateUpdateExtensions(float deltaTime);
    void renderExtensions();
    void renderUIExtensions();
};

/**
 * @brief Утилиты для создания Runtime
 */
namespace RuntimeFactory {
    // Создать рантайм для standalone плеера
    Runtime createPlayer(const std::filesystem::path& mapPath = {});
    
    // Создать рантайм для редактора с расширениями
    Runtime createEditor(const std::filesystem::path& mapPath = {});
    
    // Создать рантайм для тестирования с фикстурой
    Runtime createTest(const Fixture& fixture);
}

} // namespace game_runtime
