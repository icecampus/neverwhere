import QtQuick
import Game 1.0

Item 
{
    property GameObject gameObject: null
    property real radius: isoView.dimensions.cellSize.x / 4

    // Позиция относительно контейнера, без учета камерыc
    x: isoView.mapToScreen(gameObject.position).x - radius
    y: isoView.mapToScreen(gameObject.position).y - radius
    width: isoView.dimensions.cellSize.x * 2
    height: isoView.dimensions.cellSize.y * 4

    Image 
    {
        anchors.fill: parent
        source: "image://assetImages/9813e80b-c6f7-43f9-9f11-f074009bb8f1"
        
        
        
        
    }
}