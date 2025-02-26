import QtQuick 2.15
import QtQuick.Controls 2.15

ApplicationWindow 
{
    id: window
    visible: true
    width: 1920
    height: 1080
    title: "EpicMapEditor"

    ColorPalette
    {
        id: colorPalette
    }
   
    Workspace
    {
        id: map
        anchors.fill: parent
    }

    

    /*
    PaletteSample
    {
        anchors.fill: parent
    }
    //*/
}