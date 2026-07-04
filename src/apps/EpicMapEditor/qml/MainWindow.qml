import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.3
import Qt.labs.qmlmodels
import QtQml.Models
import UI 1.0
import "UI"

Item 
{
    id: window
    property int previousX
    property int previousY
    property alias dragElement: header.dragElement
    property alias showWindowButtons: header.showWindowButtons
    

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

    // RPC-driven chapter loading (from editor_rpc_server).
    Connections
    {
        target: rpcServer
        function onLoadChapterRequested(name, uuid)
        {
            tabsContentCreator.openChapterByUuid(name, uuid)
        }
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
        onCloseRequest:(index)=>
        {
            tabsContentCreator.tabsModel.remove(index)
            header.currentIndex = header.currentIndex - 1
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

                            element.item.openTabRequest.connect(function (chapterName, chapter) 
                            {       
                                tabsContentCreator.openChapterByUuid(chapter.name, chapter.uuid)
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
                        color: "purple"
                        Component.onCompleted: 
                        {
                            element.item.parent = worspaceWrapper
                            element.item.anchors.fill = worspaceWrapper
                            element.item.load(element.data)
                        }
                    }
                }
            }
        }
    }
  
}