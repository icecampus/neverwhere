import QtQuick
import Game 1.0
import QtQuick.Controls 2.15

Rectangle 
{
    property alias text: titleText.text
    property var mapModel: null
    property var currentAsset: null

    id: noiseGenerator
    color: colorPalette.surface2     
    border.color: colorPalette.border 
    radius: 5 

    height: 100

    NoiseGenerator
    {
        id: generator
        currentAsset: noiseGenerator.currentAsset
    }

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
    
    Rectangle
    {
        id: content
        anchors.top: title.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        color: parent.color

        Button 
        {
            anchors.centerIn: parent

            text: "Generate"
            onClicked: 
            {
                generator.generate(mapModel)
            }
        }

    }


    
}
