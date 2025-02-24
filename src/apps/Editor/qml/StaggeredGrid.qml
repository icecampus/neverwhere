import QtQuick 2.15
import QtQuick.Controls 2.15



Item {
    id: gridView
    readonly property int tileWidth: 128
    readonly property int tileHeight: 64
    property int cameraX: 0
    property int cameraY: 0
    property real cameraZoom: 1.0
    
    property point hoveredCell: Qt.point(-1, -1)
    property bool showCoordinates: false

    clip: true

    
    function screenToGrid(x, y) 
    {
        const worldX = (x - gridView.cameraX) / gridView.cameraZoom
        const worldY = (y - gridView.cameraY) / gridView.cameraZoom
        
        const isoX = (worldX / tileWidth - worldY / tileHeight)
        const isoY = (worldX / tileWidth + worldY / tileHeight)
        
        const gridX = Math.floor(isoX + 0.5)
        const gridY = Math.floor(isoY + 0.5)
        
        return Qt.point(gridX, gridY)
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        renderTarget: Canvas.FramebufferObject
        
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.strokeStyle = "#888888"
            ctx.lineWidth = 1
            
            const cols = Math.ceil(width / (tileWidth * cameraZoom)) + 2
            const rows = Math.ceil(height / (tileHeight * cameraZoom)) + 2
            
            const startCol = Math.floor(cameraX / (tileWidth * cameraZoom))
            const startRow = Math.floor(cameraY / (tileHeight * cameraZoom))
            
            for (let i = -cols; i < cols; i++) {
                for (let j = -rows; j < rows; j++) {
                    const x = (i * tileWidth + (j % 2) * tileWidth/2) * cameraZoom + cameraX
                    const y = j * tileHeight/2 * cameraZoom + cameraY
                    
                    ctx.beginPath()
                    ctx.moveTo(x + tileWidth/2 * cameraZoom, y)
                    ctx.lineTo(x + tileWidth * cameraZoom, y + tileHeight/2 * cameraZoom)
                    ctx.lineTo(x + tileWidth/2 * cameraZoom, y + tileHeight * cameraZoom)
                    ctx.lineTo(x, y + tileHeight/2 * cameraZoom)
                    ctx.closePath()
                    ctx.stroke()
                }
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        
        onPositionChanged: {
            const cell = screenToGrid(mouseX, mouseY)
            hoveredCell = cell
            showCoordinates = true
            canvas.requestPaint()
        }
        
        onExited: showCoordinates = false
    }

    // Отображение координат
    
    Rectangle {
        visible: showCoordinates
        x: mouseArea.mouseX + 15
        y: mouseArea.mouseY + 15
        width: coordText.width + 10
        height: coordText.height + 10
        color: "#aa000000"
        radius: 5
        
        Text {
            id: coordText
            anchors.centerIn: parent
            text: `Tile: ${hoveredCell.x}, ${hoveredCell.y}`
            color: "white"
            font.pixelSize: 14
        }
    }
    


    onCameraXChanged: canvas.requestPaint()
    onCameraYChanged: canvas.requestPaint()
    onCameraZoomChanged: canvas.requestPaint()
}
