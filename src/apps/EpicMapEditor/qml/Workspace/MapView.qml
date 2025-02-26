import QtQuick
import QtQuick.Controls 2.15
import Game 1.0

Rectangle 
{
    id: centerPanel
    SplitView.fillWidth: true
    color: "#ffffff"
    border.color: "#cccccc"

    StaggeredGrid 
    {
        id: gridView
    
        anchors.fill: parent
        clip: true
    }
    
    
    MouseArea 
    {
            anchors.fill: parent
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
                if (pressed) {
                    var dx = mouseX - startX
                    var dy = mouseY - startY
                    isoView.cameraX = startCamX + dx
                    isoView.cameraY = startCamY + dy
                }
            }

            onWheel: {
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
    
}
