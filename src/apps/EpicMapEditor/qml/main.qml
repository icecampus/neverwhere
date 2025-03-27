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
    x: 300
    y: 300
    width: 1920
    height: 1080
    title: "EpicMapEditor"
    
    //flags: Qt.Window | Qt.FramelessWindowHint
    //color: "transparent"
    //caption: header.dragElement


    Header
    {
        id: header
	    anchors.left: parent.left
	    anchors.right: parent.right
	    anchors.top: parent.top
    }

    function maximaze()
	{
		if(window.visibility == Window.Windowed)
		{
			window.showMaximized()
		}
		else
		{
			window.showNormal()
		}
	}
	
    ColorPalette
    {
        id: colorPalette
    }
   
    StackLayout
	{
		id: stackLayout
		anchors.top: header.bottom
		anchors.left: parent.left
		anchors.right: parent.right
		anchors.bottom: parent.bottom

		currentIndex: header.currentIndex
		
        Workspace
        {
            id: map
        }
    }
    

    /*
    PaletteSample
    {
        anchors.fill: parent
    }
    //*/
}