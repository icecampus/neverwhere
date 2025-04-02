import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.3
import Qt.labs.qmlmodels
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

    Component.onCompleted: 
    {
        core.load();
        tabsContentCreator.createOrActivateTab("Home", TabType.Home, {}, {} ) 
    }

    TabContentCreator
    {
        id: tabsContentCreator
        contentParent: stackLayout
        bar: header.bar
    }


    Header
    {
        id: header
	    anchors.left: parent.left
	    anchors.right: parent.right
	    anchors.top: parent.top

        tabsModel: tabsContentCreator.tabsModel
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
		
        Repeater
		{
			id: tabRepeater
            model: tabsContentCreator.tabsModel
            DelegateChooser 
            {
                id: chooser
                role: "type"

                DelegateChoice 
                {   
                    roleValue: TabType.Home
                    delegate: Rectangle
                    {
                        id: homeWrapper
                        color: "blue"
                        Component.onCompleted: 
                        {
                            element.item.parent = homeWrapper
                            element.item.anchors.fill = homeWrapper

                            element.item.openTabRequest.connect(function (chapterName) 
                            {       
                                tabsContentCreator.openChapterByName(chapterName)
                            })
                        }
                    }
                }
                DelegateChoice 
                {
                    
                    roleValue: TabType.Workspace
                    delegate: Rectangle
                    {
                        id: worspaceWrapper
                        color: "green"
                        Component.onCompleted: 
                        {
                            console.log(element.item)                              
                            element.item.parent = worspaceWrapper
                            element.item.anchors.fill = worspaceWrapper
                        }
                    }
                }
            }
        }
        /*
        Home
        {
            id: home
            onOpenTabRequest:(chapterName) =>
            {
                tabsContentCreator.openChapterByName(chapterName)
            }
        } */ 

    }

    /*
    Workspace
    {
        id: map
    }
    */
    
}