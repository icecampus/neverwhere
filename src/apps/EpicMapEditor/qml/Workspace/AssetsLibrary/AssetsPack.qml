import QtQuick
import QtQuick.Layouts
import QtQuick.Controls 2.15
import Game 1.0
import "../../Common"

GridView 
{
    property var assetsContext: null
    property alias assetsPackModel: paletteView.model
    
    id: paletteView
    clip: true

    property int minCellWidth: 120    // Минимальная ширина ячейки
    cellWidth: width / Math.max(1, Math.floor(width / minCellWidth))    // Динамическая ширина ячейки
    cellHeight: 120    // Фиксированная высота ячейки

    delegate: GridElement 
    {
        width: paletteView.cellWidth
        height: paletteView.cellHeight
        text: element.name
        imageSource: element.thumbnailUrl
        selected: assetsContext.asset === element

        onClicked: 
        {
            assetsContext.asset = element
        }
    }
}
