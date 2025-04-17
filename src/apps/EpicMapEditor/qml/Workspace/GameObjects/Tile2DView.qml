import QtQuick
import Game 1.0

Item 
{
    property Tile2D gameObject: null
    
    property var cellCenter: isoView.mapToField(gameObject.position)

    property ImageAsset imageAsset: core.assetsLibrary.getAsset(gameObject.assetUuid)
    property size assetSize
    property real startWidt: 0

    function updateAssetPivot(screenDelta)
    {
        var assetSize = imageAsset.getSize(isoView)
        
        console.log("newScreenPivot: " + screenDelta)
        
        imageAsset.pivot.x += (screenDelta.x/assetSize.width)
        imageAsset.pivot.y += (screenDelta.y/assetSize.height)
    }

    function updateAssetWidth(screenWidthDelta)
    {
        var assetSize = imageAsset.getSize(isoView)

        imageAsset.widthInCells = startWidt + (screenWidthDelta / isoView.dimensions.cellWidth)
        updateImageSize()
    }

    function updateFramePosition()
    {
        frame.x = - (assetSize.width * imageAsset.pivot.x)
        frame.y = - (assetSize.height * imageAsset.pivot.y)  
    }
    
    function updateFrameGeometry()
    {
        var assetSize = imageAsset.getSize(isoView)

        frame.x = - (assetSize.width * imageAsset.pivot.x)
        frame.y = - (assetSize.height * imageAsset.pivot.y)

        frame.width =  assetSize.width
        frame.height = assetSize.height

        //assetSize = imageAsset.getSize(isoView)
    }

    function updateImageSize()
    {
        assetSize = imageAsset.getSize(isoView)
    }

    Component.onCompleted: 
    {
        updateImageSize()
        updateFrameGeometry()
        frame.width =  assetSize.width
        frame.height = assetSize.height
        
        imageAsset.widthChanged.connect(updateImageSize)
        imageAsset.widthChanged.connect(updateFrameGeometry)

        imageAsset.pivotChanged.connect(updateFramePosition)
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

    Resizable 
    {
        id: frame

        onUpdateScreenDragDelta: (screenDelta)=> 
        {
            updateAssetPivot(screenDelta)
        }

        onUpdateScreenWidthDelta:(screenWidthDelta)=>
        {
            //console.log("onUpdateScreenWidthDelta: " + screenWidthDelta)
            updateAssetWidth(screenWidthDelta)
        }

        onStartDrag:
        {
            imageAsset.widthChanged.disconnect(updateImageSize)
            imageAsset.widthChanged.disconnect(updateFramePosition)
            startWidt = imageAsset.widthInCells
            
        }
        onEndDrag:
        {
            imageAsset.widthChanged.connect(updateImageSize)
            imageAsset.widthChanged.connect(updateFramePosition)
            startWidt = 0
        }
    }
    

    Image 
    {
        id: image
        x: - (assetSize.width * imageAsset.pivot.x)
        y: - (assetSize.height * imageAsset.pivot.y)
        width:  assetSize.width
        height: assetSize.height

        source: imageAsset.thumbnailUrl
    }
}