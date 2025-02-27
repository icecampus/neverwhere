import QtQuick
import Game 1.0

Rectangle {
    color: colorPalette.surface      
    border.color: colorPalette.border 

    GridView {
        id: paletteView
        anchors.fill: parent
        model: assetModel 
        property int minCellWidth: 150    // Минимальная ширина ячейки
        cellWidth: width / Math.max(1, Math.floor(width / minCellWidth))    // Динамическая ширина ячейки
        cellHeight: 150    // Фиксированная высота ячейки

        delegate: Item {
            width: paletteView.cellWidth
            height: paletteView.cellHeight

            Rectangle {
                anchors.fill: parent
                radius: 12
                color: colorPalette.surface    
                border.color: colorPalette.primaryOrange 
                
                Image {
                    anchors.centerIn: parent
                    source: element.url 
                    width: 80
                    height: 80
                    fillMode: Image.PreserveAspectFit 
                }

                Rectangle {
                    id: textBackground
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 5
                    width: textItem.width + 20
                    height: textItem.height + 10
                    color: "#80000000" // Полупрозрачный черный (50% непрозрачности)
                    radius: 4

                    Text {
                        id: textItem
                        anchors.centerIn: parent
                        text: element.name 
                        color: colorPalette.textPrimary
                        font.pixelSize: 18
                    }
                }
            }

            MouseArea {
                anchors.fill: parent
                hoverEnabled: true
                onEntered: parent.children[0].color = colorPalette.primaryOrange
                onExited: parent.children[0].color = colorPalette.surface2
                onClicked: {
                    console.log("Asset selected:", element.name)
                }
            }
        }
    }
}