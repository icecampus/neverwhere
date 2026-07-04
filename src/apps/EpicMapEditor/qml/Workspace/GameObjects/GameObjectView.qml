import QtQuick
import Game 1.0

Item 
{
    property GameObject gameObject: null
    property var asset: core.assetsLibrary.getAsset(gameObject.assetUuid)

    // Позиция относительно контейнера, без учета камерыc
    x: isoView.mapToField(gameObject.position).x 
    y: isoView.mapToField(gameObject.position).y 
    width: isoView.cellSizeX * 2
    height: isoView.cellSizeY * 4
    z: isoView.zOffset(gameObject.position)


    Image 
    {
        anchors.fill: parent
        source: gameObject.assetUuid
    }
}