import QtQuick
import Game 1.0


Item {
    id: palette

    // Основные цвета
    property color primaryOrange: "#FF8A65"    // Мягкий оранжевый
    property color darkOrange:   "#BF360C"     // Акцентный темный
    property color lightOrange: "#FFCCBC"      // Светлый акцент

    // Серые тона
    property color background:  "#121212"     // Основной фон
    property color surface:     "#1E1E1E"     // Поверхности
    property color surface2:    "#2D2D2D"     // Вторичные поверхности
    property color border:      "#404040"     // Границы
    property color textPrimary: "#E0E0E0"     // Основной текст
    property color textSecondary:"#9E9E9E"    // Вторичный текст

    // Дополнительные цвета
    property color error:       "#CF6679"     // Ошибки
    property color success:     "#81C784"     // Успех
    property color warning:     "#FFD54F"     // Предупреждения

    // Тени
    property var shadowSmall: [
        Qt.rgba(255, 255, 255, 0.05),
        Qt.rgba(255, 255, 255, 0.07),
        Qt.rgba(255, 255, 255, 0.10)
    ]

    property var shadowMedium: [
        Qt.rgba(255, 255, 255, 0.10),
        Qt.rgba(255, 255, 255, 0.12),
        Qt.rgba(255, 255, 255, 0.15)
    ]
}