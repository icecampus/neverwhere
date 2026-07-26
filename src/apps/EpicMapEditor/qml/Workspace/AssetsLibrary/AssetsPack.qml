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

    delegate: AssetView 
    {
        width: paletteView.cellWidth
        height: paletteView.cellHeight
        text: element.name
        imageSource: element.thumbnailUrl
        selected: assetsContext.asset === element

        settingsMode: element.editMode

        onClicked: 
        {
            assetsContext.asset = element
        }

        onSettingsClicked:
        {
            // The settings panel lives in the right panel and is driven by the
            // SELECTED asset, so the gear click must select the asset too.
            assetsContext.asset = element
            element.editMode = true
        }

        onSettingComplete:
        {   
            element.editMode = false
            core.assetsLibrary.save(element)
        }

        onSettingCancel:
        {   
            element.editMode = false
        }

    }
}
