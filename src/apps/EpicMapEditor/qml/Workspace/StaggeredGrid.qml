import QtQuick 2.15
import QtQuick.Controls 2.15
import Game 1.0


Item 
{
    id: gridView

    clip: true
    
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
            
            var viewSize = math.vec2(width, height) // Используем Vec2Factory
            var cameraOffset = math.vec2(isoView.cameraX, isoView.cameraY)
            var visibleRegion = isoView.getVisibleCellBounds(viewSize, cameraOffset)
            
            ctx.strokeStyle = "#666666"
            ctx.lineWidth = 1
            
            for (var y = visibleRegion.min.y; y <= visibleRegion.max.y; y++) 
            {
                for (var x = visibleRegion.min.x; x <= visibleRegion.max.x; x++) 
                {
                    var cellPos = math.ivec2(x, y)
                    var screenPos = isoView.mapToScreen(cellPos)
                    
                    
                    ctx.beginPath()
                    var top = math.vec2(screenPos.x, screenPos.y - cellHeight/2)
                    var right = math.vec2(screenPos.x + cellWidth/2, screenPos.y)
                    var bottom = math.vec2(screenPos.x, screenPos.y + cellHeight/2)
                    var left = math.vec2(screenPos.x - cellWidth/2, screenPos.y)
                    
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
}
