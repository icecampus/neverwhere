import QtQuick
import Game 1.0

Item 
{
    property GameObject gameObject: null
    property real radius: isoView.dimensions.cellSize.x / 4

    // Позиция относительно контейнера, без учета камерыc
    x: isoView.mapToField(gameObject.position).x - radius
    y: isoView.mapToField(gameObject.position).y - radius
    width: isoView.dimensions.cellSize.x * 2
    height: isoView.dimensions.cellSize.y * 4
    z: isoView.zOffset(gameObject.position)

    property int randomIndex: Math.floor(Math.random() * 5)

    Image 
    {
        anchors.fill: parent
        source: gameObject.assetUuid
    }
}