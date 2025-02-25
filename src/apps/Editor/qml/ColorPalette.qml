import QtQuick

Item 
{
    id: palette

    // Яркие оранжевые акценты
    property color primaryOrange: "#FF6D00"    // Основной акцент (чистый оранжевый)
    property color darkOrange:   "#FF3D00"     // Глубокий оранжево-красный
    property color lightOrange: "#FF9E00"     // Светящийся оранжевый
    property color neonOrange:  "#FF9100"     // Неоновое свечение

    // Темная базовая палитра
    property color background:  "#0A0A0A"     // Глубокий черный фон
    property color surface:     "#1A1A1A"     // Основные поверхности
    property color surface2:    "#2E2E2E"     // Вторичные элементы
    property color border:      "#404040"     // Границы
    property color textPrimary: "#F0F0F0"     // Основной текст
    property color textSecondary:"#A0A0A0"    // Вторичный текст

    // Дополнительные цвета
    property color error:       "#FF5252"     // Ярко-красный
    property color success:     "#76FF03"     // Лаймово-зеленый
    property color warning:     "#FFEA00"     // Ярко-желтый

    // Эффекты свечения
    property var orangeGlow: Gradient {
        GradientStop { position: 0.0; color: Qt.rgba(1, 0.4, 0, 0.2) }
        GradientStop { position: 1.0; color: "transparent" }
    }

    // Тени
    property var shadowSmall: [
        Qt.rgba(255, 165, 0, 0.15),
        Qt.rgba(255, 165, 0, 0.10),
        Qt.rgba(255, 165, 0, 0.05)
    ]
}