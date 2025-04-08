import QtQuick
import Game 1.0

    
MouseArea 
{

    property var isoView: null

    id: mouseArea
    anchors.fill: parent
    hoverEnabled: true 
    acceptedButtons: Qt.LeftButton | Qt.RightButton 

    property int startX: 0
    property int startY: 0
    property int startCamX: 0
    property int startCamY: 0
    property bool startDrag: false

    onPressed:(mouse)=>
    {
        if(mouse.button === Qt.RightButton )
        {
            startX = mouseX
            startY = mouseY
            startCamX = isoView.cameraX
            startCamY = isoView.cameraY
            
            startDrag = true
            console.log("startDrag")
        }
    }


    onReleased:(mouse)=>
    {
        if(mouse.button === Qt.RightButton )
        {
            startDrag = false
        }
    }


    onPositionChanged:(mouse)=>
    {
        if (startDrag) 
        {
            var dx = mouseX - startX
            var dy = mouseY - startY
            isoView.cameraX = startCamX + dx
            isoView.cameraY = startCamY + dy
        }

        var screenPos = math.vec2(mouseArea.mouseX, mouseArea.mouseY)
        var cellPos = isoView.screenToMap(screenPos)

        hoveredCell = math.ivec2(cellPos.x, cellPos.y) // Обновляем координаты ячейки
    }

    onWheel: (wheel) => 
    {
        var delta = wheel.angleDelta.y
        if (delta === 0) return
        
        var zoomFactor = delta > 0 ? 1.1 : 0.9
        var oldZoom = isoView.cameraZoom
        var newZoom = Math.max(0.1, Math.min(3.0, oldZoom * zoomFactor))
        
        // вычисляем позицию на карте до изменения масштаба
        var mapX = (wheel.x - isoView.cameraX) / oldZoom
        var mapY = (wheel.y - isoView.cameraY) / oldZoom
        
        // Корректируем положение камеры для сохранения позиции под курсором
        isoView.cameraX = wheel.x - mapX * newZoom
        isoView.cameraY = wheel.y - mapY * newZoom
        isoView.cameraZoom = newZoom
    }

    onClicked: (mouse) => 
    {
        // Convert mouse coordinates to map space
        var mapX = (mouse.x + isoView.cameraX) / isoView.cameraZoom
        var mapY = (mouse.y + isoView.cameraY) / isoView.cameraZoom
    }
}
