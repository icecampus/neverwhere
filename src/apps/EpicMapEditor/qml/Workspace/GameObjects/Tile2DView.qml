import QtQuick
import Game 1.0

Item 
{
    property Tile2D gameObject: null
    
    property var cellCenter: isoView.mapToField(gameObject.position)

    property ImageAsset imageAsset: core.assetsLibrary.getAsset(gameObject.assetUuid)
    //property size assetSize
    property real startWidt: 0

    function updateAssetPivot(screenDelta)
    {
        //console.log("screenDelta.x: " + screenDelta.x + ", screenDelta.y: " + screenDelta.y)

        imageAsset.pivot.x -= (screenDelta.x.toFixed()/isoView.dimensions.cellSize.x)
        imageAsset.pivot.y -= (screenDelta.y.toFixed()/isoView.dimensions.cellSize.y)
    }

    function updateAssetWidth(screenWidthDelta)
    {
        //console.log("screenDelta.x: " + screenWidthDelta)
        var assetSize = imageAsset.setScreenWidth(startWidt + screenWidthDelta,isoView)

        imageAsset.setScreenWidth(startWidt + screenWidthDelta, isoView)
    }

    function updateFrameGeometry()
    {
        var assetSize = imageAsset.getScreenSize(isoView)

        //console.log("isoView.dimensions.cellSize.x: " + isoView.dimensions.cellSize.x)
        //console.log("imageAsset.pivot.x: " + imageAsset.pivot.x)

        frame.x = (isoView.dimensions.cellSize.x * imageAsset.pivot.x) - assetSize.width/2
        frame.y = (isoView.dimensions.cellSize.y * imageAsset.pivot.y) - assetSize.height/2
        frame.width =  assetSize.width
        frame.height = assetSize.height

        //console.log("frame.x: " + frame.x)
    }


    Component.onCompleted: 
    {
        updateFrameGeometry()
        
        imageAsset.widthChanged.connect(updateFrameGeometry)
        imageAsset.pivotChanged.connect(updateFrameGeometry)
    }

    x: cellCenter.x 
    y: cellCenter.y 
    z: isoView.zOffset(gameObject.position)


    Pivot
    {
        x: -isoView.dimensions.cellSize.x/2
        y: -isoView.dimensions.cellSize.y/2
        
        width: isoView.dimensions.cellSize.x
        height: isoView.dimensions.cellSize.y
    }

    // !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
    // его позиция обновляется через pivot поинт, поэтому обновляется позиция MouseArea 
    // и при перетаскивание всего элемента приходит только дельта
    Resizable 
    {
        id: frame

        onUpdateScreenDragDelta: (screenDelta)=> 
        {
            updateAssetPivot(screenDelta)
        }

        onUpdateScreenWidthDelta:(screenWidthDelta)=>
        {
            updateAssetWidth(screenWidthDelta)
        }

        onStartDrag:
        {
            startWidt = imageAsset.getScreenSize(isoView).width
            
        }
        onEndDrag:
        {
            startWidt = 0
        }
    }
    

    Image 
    {
        id: image
        
        x:  frame.x
        y: frame.y
        width:  frame.width
        height: frame.height
        

        source: imageAsset.thumbnailUrl
    }
}