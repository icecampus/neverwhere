import QtQuick
import Game 1.0

Item
{
    property Tile2D gameObject: null

    property var cellCenter: isoView ? isoView.mapToField(gameObject.position) : Qt.point(0, 0)

    property ImageAsset imageAsset: core.assetsLibrary.getAsset(gameObject.assetUuid)
    //property size assetSize
    property real startWidt: 0

    function updateAssetPivot(screenDelta)
    {
        //console.log("screenDelta.x: " + screenDelta.x + ", screenDelta.y: " + screenDelta.y)

        imageAsset.pivot.x -= (screenDelta.x.toFixed()/isoView.cellSizeX)
        imageAsset.pivot.y -= (screenDelta.y.toFixed()/isoView.cellSizeY)
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

        //console.log("isoView.cellSizeX: " + isoView.cellSizeX)
        //console.log("imageAsset.pivot.x: " + imageAsset.pivot.x)

        frame.x = (isoView.cellSizeX * imageAsset.pivot.x) - assetSize.width/2
        frame.y = (isoView.cellSizeY * imageAsset.pivot.y) - assetSize.height/2
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
        x: -isoView.cellSizeX/2
        y: -isoView.cellSizeY/2

        width: isoView.cellSizeX
        height: isoView.cellSizeY
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

        visible: imageAsset.editMode
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