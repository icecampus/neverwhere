#include "pch.h"
#include "game_runtime/editor_extensions.h"
#include "game_runtime/runtime.h"

namespace game_runtime {

// ==================== GridEditorExtension ====================

void GridEditorExtension::render(Runtime& runtime) {
    if (!showGrid_) return;
    
    // Рендеринг сетки будет реализован через RenderExtension
    // Пока заглушка
    spdlog::trace("GridEditorExtension::render");
}

void GridEditorExtension::renderUI(Runtime& runtime) {
    // Отображаем панель настроек сетки
    // Это будет вызываться в ImGui или Qt контексте
    spdlog::trace("GridEditorExtension::renderUI");
}

// ==================== SelectionEditorExtension ====================

void SelectionEditorExtension::update(Runtime& runtime, float deltaTime) {
    // Обновление логики выделения
    // Проверка кликов, drag-and-drop и т.д.
}

void SelectionEditorExtension::render(Runtime& runtime) {
    if (!selectedCell_) return;
    
    // Рендеринг подсветки выделенной ячейки
    spdlog::trace("SelectionEditorExtension::render - selected cell: ({}, {})", 
                  selectedCell_->x, selectedCell_->y);
}

void SelectionEditorExtension::renderUI(Runtime& runtime) {
    if (!showSelectionInfo_ || !selectedCell_) return;
    
    // Отображение информации о выделенном объекте
}

// ==================== ToolsEditorExtension ====================

void ToolsEditorExtension::renderUI(Runtime& runtime) {
    // Отображение панели инструментов
    // Кнопки: Select, Brush, Eraser, Fill
}

} // namespace game_runtime
