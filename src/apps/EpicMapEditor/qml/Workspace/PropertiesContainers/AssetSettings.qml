import QtQuick
import Game 1.0
import "AssetSettings"
import "../../Common"

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
            height: 40
            width: parent.width
            Item{ height: 30; width: parent.width/2;  Text{ text: "Name: ";  anchors.verticalCenter: parent.verticalCenter;  color: colorPalette.textPrimary; font.pixelSize: 16 }} 
            Text{ width: parent.width/2; text: (asset)? asset.name : ""; anchors.verticalCenter: parent.verticalCenter;  color: colorPalette.textPrimary; font.pixelSize: 16 }
        }

        Row
        { 
            anchors.horizontalCenter: parent.horizontalCenter
            height: 40

            ImageButton
            {
                width: 40
                height: 40
                imageSource: "qrc:/resources/icons/cancel.png"
            }
            
            ImageButton
            {
                width: 40
                height: 40
                imageSource: "qrc:/resources/icons/save.png"
            }
        }

       
        Item
        {
            width: parent.width
            height:5
        }

    }

}
