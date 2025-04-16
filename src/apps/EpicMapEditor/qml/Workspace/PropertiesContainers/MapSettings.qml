import QtQuick
import Game 1.0

Rectangle 
{
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
}
