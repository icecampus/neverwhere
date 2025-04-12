import QtQuick
import Game 1.0

Item 
{
    property Landscape gameObject: null
    property SliceAsset sliceAsset: core.assetsLibrary.getAsset(gameObject.assetUuid)
    property size assetSize: sliceAsset.getSize(isoView)
    

    // Позиция относительно контейнера, без учета камерыc
    x: isoView.mapToField(gameObject.position).x - assetSize.width/2
    y: isoView.mapToField(gameObject.position).y - assetSize.height/2
    width:  assetSize.width
    height: assetSize.height
    z: isoView.zOffset(gameObject.position)

    Image 
    {
        anchors.fill: parent
        source: sliceAsset.thumbnailUrl
    }
}