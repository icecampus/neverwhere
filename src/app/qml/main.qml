import QtQuick
import QtQuick.Window
import QtQuick.Shapes 1.15

Window {
    width: 800
    height: 600
    visible: true
    title: "Isometric Map with Camera Control"

    property int tileWidth: 128
    property int tileHeight: 64
    property int cameraX: -300
    property int cameraY: -200
    property real cameraZoom: 1.0

    Component {
        id: tileComponent
        Item {
            property bool selected: false
            x: (gridX - gridY) * tileWidth / 2
            y: (gridX + gridY) * tileHeight / 2

            width: tileWidth
            height: tileHeight

            Shape {
                anchors.fill: parent
                ShapePath {
                    fillColor: selected ? "darkblue" : "lightblue"
                    strokeColor: "darkblue"
                    strokeWidth: 2
                    PathPolyline {
                        path: [
                            Qt.point(width/2, 0),
                            Qt.point(width, height/2),
                            Qt.point(width/2, height),
                            Qt.point(0, height/2),
                            Qt.point(width/2, 0)
                        ]
                    }
                }
            }
        }
    }

    Item {
        anchors.fill: parent
        clip: true

        Item {
            transform: [
                Scale { xScale: cameraZoom; yScale: cameraZoom },
                Translate { x: -cameraX; y: -cameraY }
            ]

            Repeater {
                id: mapRepeater
                model: [
                    {x:0,y:0}, {x:1,y:0}, {x:2,y:0}, {x:3,y:0}, {x:4,y:0},
                    {x:0,y:1}, {x:1,y:1}, {x:2,y:1}, {x:3,y:1}, {x:4,y:1},
                    {x:0,y:2}, {x:1,y:2}, {x:2,y:2}, {x:3,y:2}, {x:4,y:2},
                    {x:0,y:3}, {x:1,y:3}, {x:2,y:3}, {x:3,y:3}, {x:4,y:3},
                    {x:0,y:4}, {x:1,y:4}, {x:2,y:4}, {x:3,y:4}, {x:4,y:4},
                ]

                delegate: Loader {
                    sourceComponent: tileComponent
                    property int gridX: modelData.x
                    property int gridY: modelData.y
                    z: gridX + gridY
                }
            }
        }

        MouseArea {
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
}