import QtQuick
import Game 1.0

Rectangle 
{
    color: colorPalette.surface     
    border.color: colorPalette.border 

    Text 
    {
        anchors.centerIn: parent
        text: "Left Panel"
        font.pixelSize: 16
        color: colorPalette.textPrimary
    }
}