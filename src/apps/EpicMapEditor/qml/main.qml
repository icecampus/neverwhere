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
    x: 400
    y: 200
    width: 1920
    height: 1080
    title: "EpicMapEditor"
    
    caption: header.dragElement

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

    Header
    {
        id: header
	    anchors.left: parent.left
	    anchors.right: parent.right
	    anchors.top: parent.top
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
    Rectangle 
    {
        id: captionItem
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 30  
        color: "red"
    }
    */
}