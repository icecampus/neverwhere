import QtQuick
import Game 1.0

Item 
{
    property Tile2D gameObject: null
    property ImageAsset imageAsset: core.assetsLibrary.getAsset(gameObject.assetUuid)
    property size assetSize: imageAsset.getSize(isoView)

    // Позиция относительно контейнера, без учета камерыc
    x: isoView.mapToField(gameObject.position).x - (assetSize.width * imageAsset.pivot.x)
    y: isoView.mapToField(gameObject.position).y - (assetSize.height * imageAsset.pivot.y)
    width:  assetSize.width
    height: assetSize.height
    z: isoView.zOffset(gameObject.position)

    Image 
    {
        anchors.fill: parent
        source: imageAsset.thumbnailUrl
    }
}