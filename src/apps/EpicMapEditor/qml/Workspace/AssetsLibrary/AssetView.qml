import QtQuick
import QtQuick.Layouts
import QtQuick.Controls 2.15
import Game 1.0
import "../../Common"

GridElement
{
    id: assetView
    signal settingsClicked()
    signal settingComplete()

    property bool settingsMode: false

    ImageButton
    {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 10
        anchors.topMargin: 10
        width: 40
        height: 40
        imageSource: "qrc:/resources/icons/settings.png"
        visible: !settingsMode
        //opacity: 0.7    
    
        backroundColor: colorPalette.surface2   
        onClicked: settingsClicked()
    }


    ImageButton
    {
        id: saveButton
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.rightMargin: 10
        anchors.topMargin: 10
        width: 40
        height: 40
        imageSource: "qrc:/resources/icons/save.png"
        visible: settingsMode
        //opacity: 0.7    
    
        backroundColor: colorPalette.surface2   
        onClicked: settingComplete()
    }

    ImageButton
    {
        anchors.right: saveButton.left
        anchors.top: parent.top
        anchors.rightMargin: 10
        anchors.topMargin: 10
        width: 40
        height: 40
        imageSource: "qrc:/resources/icons/cancel.png"
        visible: settingsMode
        //opacity: 0.7    
    
        backroundColor: colorPalette.surface2   
        onClicked: settingComplete()
    }

}