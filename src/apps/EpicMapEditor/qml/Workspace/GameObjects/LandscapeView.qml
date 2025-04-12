import QtQuick
import Game 1.0

Item 
{
    property Landscape gameObject: null
    property SliceAsset sliceAsset: core.assetsLibrary.getAsset(gameObject.assetUuid)
    

    // Позиция относительно контейнера, без учета камерыc
    x: isoView.mapToField(gameObject.position).x - isoView.dimensions.cellSize.x/2
    y: isoView.mapToField(gameObject.position).y - isoView.dimensions.cellSize.y/2
    width:  isoView.dimensions.cellSize.x
    height: isoView.dimensions.cellSize.y
    z: isoView.zOffset(gameObject.position)

    Image 
    {
        anchors.fill: parent
        source: sliceAsset.thumbnailUrl
    }
}