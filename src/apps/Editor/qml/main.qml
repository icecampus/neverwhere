import QtQuick 2.15
import QtQuick.Controls 2.15


import "map"

ApplicationWindow 
{
    id: window
    visible: true
    width: 1920
    height: 1080
    title: "Isometric Grid"

    readonly property int tileWidth: 128
    readonly property int tileHeight: 64
    property int cameraX: -300
    property int cameraY: -200
    property real cameraZoom: 1.0


    /*
    Map
    {
        id: map
        anchors.fill: parent
    }
    */

    PaletteSample
    {
        anchors.fill: parent
    }
}