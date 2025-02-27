import QtQuick
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 
import Game 1.0

Rectangle 
{
    id: centerPanel
    SplitView.fillWidth: true
    color: "#ffffff"
    border.color: "#cccccc"
    clip: true

    property point hoveredCell: Qt.point(-1, -1)
    property bool showCoordinates: true

    Component.onCompleted: 
    {
        mapModel.populateMapModel();
    }

    MapModel
    {
        id: mapModel
    }

    Item 
    {
        id: mapContainer
                
        x: isoView.cameraX
        y: isoView.cameraY
        scale: isoView.cameraZoom

        width: 2000
        height: 2000

        Repeater {
            model: mapModel
            delegate: Item 
            {
                property GameObject gameObject: model.element
                property real radius: isoView.dimensions.cellSize.x / 4

                // Позиция относительно контейнера, без учета камерыc
                x: isoView.mapToScreen(gameObject.position).x - radius
                y: isoView.mapToScreen(gameObject.position).y - radius
                width: 2 * radius
                height: 2 * radius

                Rectangle 
                {
                    anchors.fill: parent
                    radius: width / 2 // Делаем круглым
                    color: "red" // Цвет можно изменить
                    border.color: "black"
                    border.width: 1
                }
            }
        }
    }

    StaggeredGrid 
    {
        id: gridView
    
        anchors.fill: parent
        clip: true
    }
    
    MouseArea 
    {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true // Включаем отслеживание движения курсора

        property int startX: 0
        property int startY: 0
        property int startCamX: 0
        property int startCamY: 0

        onPressed: {
            startX = mouseX
            startY = mouseY
            startCamX = isoView.cameraX
            startCamY = isoView.cameraY
        }

        onPositionChanged: 
        {
            if (pressed) 
            {
                var dx = mouseX - startX
                var dy = mouseY - startY
                isoView.cameraX = startCamX + dx
                isoView.cameraY = startCamY + dy
            }

            var screenPos = math.vec2(mouseArea.mouseX, mouseArea.mouseY)
            var cellPos = isoView.screenToMap(screenPos)
            hoveredCell = Qt.point(cellPos.x, cellPos.y) // Обновляем координаты ячейки
        }

        onWheel: (wheel) => {
            var delta = wheel.angleDelta.y
            if (delta === 0) return

            var zoomFactor = delta > 0 ? 1.1 : 0.9
            var oldZoom = isoView.cameraZoom
            var newZoom = Math.max(0.5, Math.min(3.0, oldZoom * zoomFactor))

            var mapX = (wheel.x + isoView.cameraX) / oldZoom
            var mapY = (wheel.y + isoView.cameraY) / oldZoom

            isoView.cameraX = mapX * newZoom - wheel.x
            isoView.cameraY = mapY * newZoom - wheel.y
            isoView.cameraZoom = newZoom
        }

        onClicked: (mouse) => {
            // Convert mouse coordinates to map space
            var mapX = (mouse.x + isoView.cameraX) / isoView.cameraZoom
            var mapY = (mouse.y + isoView.cameraY) / isoView.cameraZoom
        }
    }


    // Стилизованное отображение координат
    Item {
        id: coordinateDisplay
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        width: 100
        height: 30
        
        Rectangle {
            id: background
            anchors.fill: parent
            color: colorPalette.surface // Используем цвет фона из палитры
            opacity: 0.7 // Полупрозрачность
            radius: 5 // Скругленные углы
        }
        
        Text {
            id: coordinateText
            anchors.centerIn: parent
            color: colorPalette.textPrimary // Цвет текста из палитры
            font.pixelSize: 16
            text: showCoordinates ? "(" + hoveredCell.x + ", " + hoveredCell.y + ")" : ""
        }
        
        Glow {
            anchors.fill: coordinateText
            radius: 3
            samples: 17
            color: colorPalette.neonOrange // Свечение из палитры
            source: coordinateText
            visible: showCoordinates
        }
    }
    
}
