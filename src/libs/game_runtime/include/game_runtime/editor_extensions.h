#pragma once

#include "pch.h"

namespace game_runtime {

// Forward declarations
class Runtime;
class GameSession;
class GameWorld;

/**
 * @brief Интерфейс расширения рантайма
 * 
 * Позволяет добавлять дополнительную функциональность к рантайму.
 * Используется редактором для добавления инструментов редактирования.
 */
class IRuntimeExtension {
public:
    virtual ~IRuntimeExtension() = default;
    
    // Имя расширения
    virtual const char* name() const = 0;
    
    // Инициализация/деинициализация
    virtual void initialize(Runtime& runtime) {}
    virtual void shutdown(Runtime& runtime) {}
    
    // Жизненный цикл сессии
    virtual void onSessionCreated(GameSession& session) {}
    virtual void onSessionDestroyed(GameSession& session) {}
    
    // Обновление
    virtual void update(Runtime& runtime, float deltaTime) {}
    virtual void lateUpdate(Runtime& runtime, float deltaTime) {}
    
    // Рендеринг (если нужно)
    virtual void render(Runtime& runtime) {}
    virtual void renderUI(Runtime& runtime) {}
    
    // Обработка событий
    // virtual void onEvent(Runtime& runtime, const Event& event) {}
    
    // Включено ли расширение
    virtual bool isEnabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }

protected:
    bool enabled_ = true;
};

/**
 * @brief Фабрика расширений
 */
using ExtensionFactory = std::function<std::unique_ptr<IRuntimeExtension>()>;

/**
 * @brief Расширение для редактора - базовый класс
 */
class EditorExtension : public IRuntimeExtension {
public:
    const char* name() const override { return "EditorExtension"; }
    
    // Отображается ли UI редактора
    virtual bool showEditorUI() const { return true; }
    
    // Режим редактирования
    virtual bool isEditMode() const { return editMode_; }
    void setEditMode(bool edit) { editMode_ = edit; }

protected:
    bool editMode_ = true;
};

/**
 * @brief Расширение сетки редактора
 */
class GridEditorExtension : public EditorExtension {
public:
    const char* name() const override { return "GridEditor"; }
    
    void render(Runtime& runtime) override;
    void renderUI(Runtime& runtime) override;

private:
    bool showGrid_ = true;
    bool showCoordinates_ = true;
    glm::vec4 gridColor_ = glm::vec4(0.5f, 0.5f, 0.5f, 0.3f);
};

/**
 * @brief Расширение выделения объектов
 */
class SelectionEditorExtension : public EditorExtension {
public:
    const char* name() const override { return "SelectionEditor"; }
    
    void update(Runtime& runtime, float deltaTime) override;
    void render(Runtime& runtime) override;
    void renderUI(Runtime& runtime) override;

private:
    std::optional<glm::ivec2> selectedCell_;
    std::vector<glm::ivec2> multiSelection_;
    bool showSelectionInfo_ = true;
};

/**
 * @brief Расширение инструментов редактора
 */
class ToolsEditorExtension : public EditorExtension {
public:
    const char* name() const override { return "ToolsEditor"; }
    
    void renderUI(Runtime& runtime) override;
    
    enum class Tool {
        Select,
        Brush,
        Eraser,
        Fill
    };
    
    Tool currentTool() const { return currentTool_; }
    void setTool(Tool tool) { currentTool_ = tool; }

private:
    Tool currentTool_ = Tool::Select;
    std::string selectedAssetUuid_;
};

} // namespace game_runtime
