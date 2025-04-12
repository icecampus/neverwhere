import QtQuick
import Game 1.0

Item 
{
    property Tile2D gameObject: null
    property ImageAsset imageAsset: core.assetsLibrary.getAsset(gameObject.assetUuid)
    property size assetSize: imageAsset.getSize(isoView)

    // Позиция относительно контейнера, без учета камерыc
    x: isoView.mapToField(gameObject.position).x - assetSize.width/2
    y: isoView.mapToField(gameObject.position).y - assetSize.height
    width:  assetSize.width
    height: assetSize.height
    z: isoView.zOffset(gameObject.position)

    Image 
    {
        anchors.fill: parent
        source: imageAsset.thumbnailUrl
    }

    Rectangle
    {
        anchors.centerIn: parent
        width: 5
        height: 5
        color: "red"
    }

}