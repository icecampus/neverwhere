import QtQuick
import Game 1.0

Rectangle 
{
    property var asset: null
    property alias text: titleText.text

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
        anchors.top: title.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        
        anchors.leftMargin: 5
        anchors.topMargin: 5

        Row
        {
            height: 40
            width: parent.width
            Item{ height: 30; width: parent.width/2;  Text{ text: "Name: ";  anchors.verticalCenter: parent.verticalCenter;  color: colorPalette.textPrimary; font.pixelSize: 16 }} 
            Text{ width: parent.width/2; text: asset.name; anchors.verticalCenter: parent.verticalCenter;  color: colorPalette.textPrimary; font.pixelSize: 16 }
        }

        Row
        {
            height: 40
            width: parent.width
            Item{ height: 30; width: parent.width/2;  Text{ text: "Width: ";  anchors.verticalCenter: parent.verticalCenter;  color: colorPalette.textPrimary; font.pixelSize: 16 }} 
            PropertyInput{width: parent.width/2}
        }


    }

}
