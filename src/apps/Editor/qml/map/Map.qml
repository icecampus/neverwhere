import QtQuick
import QtQuick.Controls 2.15
import Game 1.0

 SplitView 
{
    orientation: Qt.Horizontal

    // Левая панель
    Rectangle {
        id: leftPanel
        SplitView.preferredWidth: 200
        SplitView.minimumWidth: 100
        SplitView.maximumWidth: 300
        color: "#f0f0f0"
        border.color: "#cccccc"

        Text {
            anchors.centerIn: parent
            text: "Left Panel"
            font.pixelSize: 16
        }
    }

    // Центральная панель
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

            cameraX: -window.cameraX
            cameraY: -window.cameraY
            cameraZoom: window.cameraZoom 
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
                    startCamX = cameraX
                    startCamY = cameraY
                }

                onPositionChanged: {
                    if (pressed) {
                        var dx = mouseX - startX
                        var dy = mouseY - startY
                        cameraX = startCamX - dx
                        cameraY = startCamY - dy
                    }
                }

                onWheel: {
                    var delta = wheel.angleDelta.y
                    if (delta === 0) return

                    var zoomFactor = delta > 0 ? 1.1 : 0.9
                    var oldZoom = cameraZoom
                    var newZoom = Math.max(0.5, Math.min(3.0, oldZoom * zoomFactor))

                    var mapX = (wheel.x + cameraX) / oldZoom
                    var mapY = (wheel.y + cameraY) / oldZoom

                    cameraX = mapX * newZoom - wheel.x
                    cameraY = mapY * newZoom - wheel.y
                    cameraZoom = newZoom
                }

                onClicked: (mouse) => {
                    // Convert mouse coordinates to map space
                    var mapX = (mouse.x + cameraX) / cameraZoom
                    var mapY = (mouse.y + cameraY) / cameraZoom

                    // Check all tiles for hit
                    for (var i = 0; i < mapRepeater.count; ++i) {
                        var loader = mapRepeater.itemAt(i)
                        if (!loader || !loader.item) continue

                        var tile = loader.item
                        var gridX = loader.gridX
                        var gridY = loader.gridY

                        // Calculate tile position
                        var tileX = (gridX - gridY) * tileWidth / 2
                        var tileY = (gridX + gridY) * tileHeight / 2

                        // Convert to local tile coordinates
                        var localX = mapX - tileX
                        var localY = mapY - tileY

                        // Check if point is inside rhombus
                        var halfWidth = tileWidth / 2
                        var halfHeight = tileHeight / 2
                        var dx = Math.abs(localX - halfWidth)
                        var dy = Math.abs(localY - halfHeight)
                    
                        if ((dx / halfWidth) + (dy / halfHeight) <= 1) {
                            tile.selected = !tile.selected
                            break
                        }
                    }
                }
        }
    }

    // Правая панель
    Rectangle {
        id: rightPanel
        SplitView.preferredWidth: 200
        SplitView.minimumWidth: 100
        SplitView.maximumWidth: 300
        color: "#f0f0f0"
        border.color: "#cccccc"

        Text {
            anchors.centerIn: parent
            text: "Right Panel"
            font.pixelSize: 16
        }
    }

    handle: Rectangle {
        implicitWidth: 8
        color: SplitHandle.pressed ? "#aaaaaa" : "#cccccc"
    }
}