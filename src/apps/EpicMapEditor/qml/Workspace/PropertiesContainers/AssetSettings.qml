import QtQuick
import Game 1.0
import "AssetSettings"

Rectangle 
{
    property var asset: null
    property alias text: titleText.text

    id: assetSettings
    height: title.height + content.height 
    color: colorPalette.surface2     
    border.color: colorPalette.border 
    
    radius: 5 
    
    Rectangle
    {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 30
        color: "#80000000"
        radius: 5 

        Text
        {
            id: titleText     
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 5

            color: colorPalette.textPrimary
            font.pixelSize: 16
        }
    }
    
    Column 
    {
        id: content
        anchors.top: title.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        
        
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        anchors.topMargin: 5
        

        spacing: 5
        Row
        {
            height: 38
            width: parent.width
            Item{ height: 30; width: parent.width/2;  Text{ text: "Name: ";  anchors.verticalCenter: parent.verticalCenter;  color: colorPalette.textPrimary; font.pixelSize: 16 }} 
            Text{ width: parent.width/2; text: asset.name; anchors.verticalCenter: parent.verticalCenter;  color: colorPalette.textPrimary; font.pixelSize: 16 }
        }

        Row
        {
            height: 38
            width: parent.width
            Item{ height: 30; width: parent.width/2;  Text{ text: "Width: ";  anchors.verticalCenter: parent.verticalCenter;  color: colorPalette.textPrimary; font.pixelSize: 16 }} 
            PropertyInput{width: parent.width/2}
        }

        Rectangle
        {
            width: parent.width
            height: width
            color: colorPalette.surface

            PivotPointSettings
            {
                property var internalAsset: assetSettings.asset
                onInternalAssetChanged:
                {
                    if(assetSettings.asset)
                    {
                        updatePivot(assetSettings.asset.pivot)
                    }
                }   
                id: pivotPointSettings
                anchors.fill: parent
                imageSource: (asset)? asset.thumbnailUrl : ""
                onPivotXChanged:
                {
                    if(asset)
                    {
                        asset.pivot = math.vec2(pivotX, pivotY)
                    }
                }

                onPivotYChanged:
                {
                    if(asset)
                    {
                        asset.pivot = math.vec2(pivotX, pivotY)
                    }
                }
            }
        }
        
        Item
        {
            width: parent.width
            height:5
        }
    }

}
