import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.3
import UI 1.0
import "UI"

EpicEditorWindow 
{
    id: window
    property int previousX
    property int previousY

    visible: true
    width: 1920
    height: 1080
    title: "EpicMapEditor"
    
    //flags: Qt.Window | Qt.FramelessWindowHint
    //color: "transparent"
    caption: captionItem

    Rectangle 
    {
        id: captionItem
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 30  
        color: "red"
    }
}