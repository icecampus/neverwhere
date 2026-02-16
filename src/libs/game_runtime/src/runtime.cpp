#include "pch.h"
#include "game_runtime/runtime.h"

namespace game_runtime {

Runtime* Runtime::currentRuntime_ = nullptr;

Runtime::Runtime(const RuntimeConfig& config) : config_(config) {
    spdlog::debug("Runtime created");
}

Runtime::~Runtime() {
    if (initialized_) {
        shutdown();
    }
    if (currentRuntime_ == this) {
        currentRuntime_ = nullptr;
    }
}

bool Runtime::initialize() {
    if (initialized_) {
        return true;
    }
    
    spdlog::info("Initializing Runtime...");
    currentRuntime_ = this;
    
    // Разрешаем пути
    if (config_.dataRoot.empty()) {
        config_.dataRoot = std::filesystem::current_path();
    }
    
    if (config_.assetsRoot.is_relative()) {
        config_.assetsRoot = config_.dataRoot / config_.assetsRoot;
    }
    if (config_.defaultMap.is_relative()) {
        config_.defaultMap = config_.dataRoot / config_.defaultMap;
    }
    
    // Инициализируем расширения редактора если нужно
    if (config_.enableEditorExtensions) {
        initializeExtensions();
    }
    
    initialized_ = true;
    
    // Вызываем коллбэк
    if (config_.onInitialized) {
        config_.onInitialized(*this);
    }
    
    spdlog::info("Runtime initialized successfully");
    return true;
}

void Runtime::shutdown() {
    if (!initialized_) {
        return;
    }
    
    spdlog::info("Shutting down Runtime...");
    
    // Останавливаем сессию
    if (currentSession_) {
        currentSession_->stop();
        currentSession_.reset();
    }
    
    // Выгружаем расширения
    shutdownExtensions();
    
    // Вызываем коллбэк
    if (config_.onShutdown) {
        config_.onShutdown(*this);
    }
    
    initialized_ = false;
    
    if (currentRuntime_ == this) {
        currentRuntime_ = nullptr;
    }
    
    spdlog::info("Runtime shut down");
}

void Runtime::run() {
    if (!initialized_ && !initialize()) {
        spdlog::error("Failed to initialize Runtime");
        return;
    }
    
    // Создаем начальную сессию
    if (!currentSession_) {
        if (config_.initialFixture) {
            createSession(*config_.initialFixture);
        } else {
            createSession();
        }
    }
    
    // Главный цикл
    auto lastTime = std::chrono::steady_clock::now();
    
    while (!shouldExit_) {
        auto currentTime = std::chrono::steady_clock::now();
        float deltaTime = std::chrono::duration<float>(currentTime - lastTime).count();
        lastTime = currentTime;
        
        // Фиксированный timestep
        if (config_.fixedTimeStep) {
            deltaTime = config_.fixedDeltaTime;
        }
        
        // Обновление
        update(deltaTime);
        
        // Рендеринг
        render();
        
        // Ограничение FPS
        if (config_.targetFps > 0) {
            float targetFrameTime = 1.0f / config_.targetFps;
            float elapsed = std::chrono::duration<float>(
                std::chrono::steady_clock::now() - currentTime).count();
            if (elapsed < targetFrameTime) {
                std::this_thread::sleep_for(
                    std::chrono::duration<float>(targetFrameTime - elapsed));
            }
        }
    }
}

void Runtime::update(float deltaTime) {
    deltaTime_ = deltaTime;
    totalTime_ += deltaTime;
    frameCount_++;
    
    // Обновляем сессию
    if (currentSession_ && currentSession_->isRunning()) {
        currentSession_->update(deltaTime);
    }
    
    // Обновляем расширения
    updateExtensions(deltaTime);
    lateUpdateExtensions(deltaTime);
}

void Runtime::render() {
    // Рендерим мир через расширения
    renderExtensions();
    renderUIExtensions();
}

void Runtime::processEvents() {
    // Обработка событий платформы
    // Зависит от конкретной реализации (Sokol, Qt, etc.)
}

GameSession* Runtime::createSession(const Fixture& fixture) {
    currentSession_ = std::make_unique<GameSession>(*this);
    
    // Уведомляем расширения
    for (auto* ext : enabledExtensions_) {
        ext->onSessionCreated(*currentSession_);
    }
    
    currentSession_->initialize(fixture);
    
    spdlog::info("Created game session with fixture: {}", fixture.name());
    return currentSession_.get();
}

GameSession* Runtime::createSession() {
    // Создаем сессию с пустой фикстурой или загружаем карту по умолчанию
    Fixture fixture;
    fixture.setName("default");
    fixture.setMapPath(config_.defaultMap.string());
    return createSession(fixture);
}

void Runtime::destroySession(GameSession* session) {
    if (currentSession_.get() == session) {
        // Уведомляем расширения
        for (auto* ext : enabledExtensions_) {
            ext->onSessionDestroyed(*currentSession_);
        }
        
        currentSession_->stop();
        currentSession_.reset();
        spdlog::info("Game session destroyed");
    }
}

void Runtime::applyFixture(const Fixture& fixture) {
    if (currentSession_) {
        currentSession_->initialize(fixture);
    } else {
        createSession(fixture);
    }
}

Fixture Runtime::captureCurrentState() const {
    if (currentSession_) {
        return currentSession_->capture();
    }
    return Fixture::create().emptyWorld().build();
}

void Runtime::registerExtension(std::unique_ptr<IRuntimeExtension> extension) {
    std::string name = extension->name();
    extensions_[name] = std::move(extension);
    spdlog::debug("Registered extension: {}", name);
}

void Runtime::registerExtensionFactory(const std::string& name, ExtensionFactory factory) {
    extensionFactories_[name] = std::move(factory);
}

IRuntimeExtension* Runtime::getExtension(const std::string& name) {
    auto it = extensions_.find(name);
    if (it != extensions_.end()) {
        return it->second.get();
    }
    return nullptr;
}

void Runtime::enableExtension(const std::string& name, bool enable) {
    auto* ext = getExtension(name);
    if (ext) {
        ext->setEnabled(enable);
        
        // Обновляем список активных расширений
        auto it = std::find(enabledExtensions_.begin(), enabledExtensions_.end(), ext);
        if (enable && it == enabledExtensions_.end()) {
            enabledExtensions_.push_back(ext);
            ext->initialize(*this);
        } else if (!enable && it != enabledExtensions_.end()) {
            ext->shutdown(*this);
            enabledExtensions_.erase(it);
        }
    }
}

std::vector<std::string> Runtime::getExtensionNames() const {
    std::vector<std::string> names;
    names.reserve(extensions_.size());
    for (const auto& [name, _] : extensions_) {
        names.push_back(name);
    }
    return names;
}

void Runtime::enableEditorMode() {
    config_.enableEditorExtensions = true;
    initializeExtensions();
}

void Runtime::disableEditorMode() {
    config_.enableEditorExtensions = false;
    shutdownExtensions();
}

void Runtime::initializeExtensions() {
    // Создаем расширения из фабрик
    for (const auto& [name, factory] : extensionFactories_) {
        if (extensions_.find(name) == extensions_.end()) {
            registerExtension(factory());
        }
    }
    
    // Инициализируем включенные расширения
    for (const auto& [name, ext] : extensions_) {
        if (ext->isEnabled()) {
            ext->initialize(*this);
            enabledExtensions_.push_back(ext.get());
        }
    }
}

void Runtime::shutdownExtensions() {
    for (auto* ext : enabledExtensions_) {
        ext->shutdown(*this);
    }
    enabledExtensions_.clear();
}

void Runtime::updateExtensions(float deltaTime) {
    for (auto* ext : enabledExtensions_) {
        if (ext->isEnabled()) {
            ext->update(*this, deltaTime);
        }
    }
}

void Runtime::lateUpdateExtensions(float deltaTime) {
    for (auto* ext : enabledExtensions_) {
        if (ext->isEnabled()) {
            ext->lateUpdate(*this, deltaTime);
        }
    }
}

void Runtime::renderExtensions() {
    for (auto* ext : enabledExtensions_) {
        if (ext->isEnabled()) {
            ext->render(*this);
        }
    }
}

void Runtime::renderUIExtensions() {
    for (auto* ext : enabledExtensions_) {
        if (ext->isEnabled()) {
            ext->renderUI(*this);
        }
    }
}

// ==================== RuntimeFactory ====================

namespace RuntimeFactory {

Runtime createPlayer(const std::filesystem::path& mapPath) {
    RuntimeConfig config;
    config.windowTitle = "Game Player";
    config.enableEditorExtensions = false;
    
    if (!mapPath.empty()) {
        config.defaultMap = mapPath;
    }
    
    return Runtime(config);
}

Runtime createEditor(const std::filesystem::path& mapPath) {
    RuntimeConfig config;
    config.windowTitle = "Game Editor";
    config.enableEditorExtensions = true;
    
    if (!mapPath.empty()) {
        config.defaultMap = mapPath;
    }
    
    return Runtime(config);
}

Runtime createTest(const Fixture& fixture) {
    RuntimeConfig config;
    config.windowTitle = "Game Test";
    config.enableEditorExtensions = false;
    config.initialFixture = fixture;
    config.targetFps = 0; // Без ограничения для тестов
    
    return Runtime(config);
}

} // namespace RuntimeFactory

} // namespace game_runtime
