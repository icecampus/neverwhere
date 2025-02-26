import QtQuick 2.15
import QtQuick.Controls 2.15
import Game 1.0


Item {
    id: gridView
    
    
    property point hoveredCell: Qt.point(-1, -1)
    property bool showCoordinates: true

    clip: true
    
    Component.onCompleted: 
    {
        var cellPos = ivec2Factory.create(1, 1)
        var screenPos = isoView.mapToScreen(cellPos)

    }
    



    Canvas 
    {
        id: canvas
        anchors.fill: parent
        
        // Реакция на изменения параметров
        Connections {
            target: isoView
            function onCameraXChanged() { canvas.requestPaint() }
            function onCameraYChanged() { canvas.requestPaint() }
            function onCameraZoomChanged() { canvas.requestPaint() }
        }
        
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()

        onPaint: 
        {
                
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            
            var cellWidth = isoView.dimensions.cellWidth * isoView.cameraZoom
            var cellHeight = cellWidth / isoView.dimensions.aspectRatio
            
            var viewSize = vec2Factory.create(width, height) // Используем Vec2Factory
            var cameraOffset = vec2Factory.create(isoView.cameraX, isoView.cameraY)
            var visibleRegion = isoView.getVisibleCellBounds(viewSize, cameraOffset)
            
            ctx.strokeStyle = "#666666"
            ctx.lineWidth = 1
            
            for (var y = visibleRegion.min.y; y <= visibleRegion.max.y; y++) 
            {
                for (var x = visibleRegion.min.x; x <= visibleRegion.max.x; x++) 
                {
                    var cellPos = ivec2Factory.create(x, y)
                    var screenPos = isoView.mapToScreen(cellPos)
                    
                    
                    ctx.beginPath()
                    var top = vec2Factory.create(screenPos.x, screenPos.y - cellHeight/2)
                    var right = vec2Factory.create(screenPos.x + cellWidth/2, screenPos.y)
                    var bottom = vec2Factory.create(screenPos.x, screenPos.y + cellHeight/2)
                    var left = vec2Factory.create(screenPos.x - cellWidth/2, screenPos.y)
                    
                    ctx.moveTo(top.x, top.y)
                    ctx.lineTo(right.x, right.y)
                    ctx.lineTo(bottom.x, bottom.y)
                    ctx.lineTo(left.x, left.y)
                    ctx.closePath()
                    ctx.stroke()
                    
                }
            }
        }
    }
    
    MouseArea 
    {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true // Включаем отслеживание движения курсора
        
        onPositionChanged: {
            var screenPos = vec2Factory.create(mouseArea.mouseX, mouseArea.mouseY)
            var cellPos = isoView.screenToMap(screenPos)
            hoveredCell = Qt.point(cellPos.x, cellPos.y) // Обновляем координаты ячейки
        }
    }
    
    // Text для отображения координат
    Text {
        id: coordinateText
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        color: "white"
        font.pixelSize: 16
        text: showCoordinates ? "(" + hoveredCell.x + ", " + hoveredCell.y + ")" : ""
    }
    

}
