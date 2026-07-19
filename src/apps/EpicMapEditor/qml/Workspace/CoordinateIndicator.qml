import QtQuick
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 
import Game 1.0


Item 
{
    id: coordinateDisplay
        
    Rectangle 
    {
        id: background
        anchors.fill: parent
        color: colorPalette.surface2 // Используем цвет фона из палитры
        opacity: 0.7 // Полупрозрачность
        radius: 5 // Скругленные углы
    }
        
    Image
    {
        id: icon
        anchors.left: parent.left
        anchors.verticalCenter: parent.verticalCenter
		width: 20
		height: 20
        anchors.leftMargin: 10

        source: "qrc:/resources/icons/coordinate_system.png"
    }
    
    Text 
    {
        id: coordinateText
        anchors.left: icon.right
        anchors.leftMargin: 4
        anchors.verticalCenter: parent.verticalCenter
        color: colorPalette.textPrimary // Цвет текста из палитры
        font.pixelSize: 16
        text: showCoordinates ? "(" + hoveredCell.x + ", " + hoveredCell.y + ")" : ""
    }
        
    // Glow (Qt5Compat.GraphicalEffects) removed: shader-effect items broke
    // rendering of the whole overlay on the current GL/RHI path.
    // The plain text stays readable on the translucent plate.
}
