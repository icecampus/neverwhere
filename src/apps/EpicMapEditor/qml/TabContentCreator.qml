import QtQuick
import QtQuick.Controls 2.15
import Game 1.0
import UI 1.0

Item
{
    property alias tabsModel: tabsModel
    property var contentParent: null
    property var bar: null

    function openChapterByName(name) 
    {
        if (name)
        {
            createOrActivateTab(name, TabType.Workspace, {}, {})
        }    
        else
        {
            //newMapPopup.open()
        }
    }

    function createOrActivateTab(name, type, data, extraData) 
    {
        var contentElement = tabsModel.getElementByName(name)

        if (contentElement) 
        {
            contentElement.extraData = extraData
            contentElement.activate()
        } 
        else 
        {
            var result = tabsModel.add(type, name, false, data, extraData)
            if (result) 
            {
                bar.setCurrentIndex(bar.count - 1)
            }
        }
    }

    function createContentItem(type) 
    {
        var component = getComponent(type)
        var item = component.createObject(creator)
        return item
    }

    function getComponent(type) 
    {
        switch (type) 
        {
            case TabType.Home:
                return homeComponent
            case TabType.Workspace:
                return workspaceComponent
            default:
                return homeComponent
        }
    }

    id: creator

    TabsModel
    {
        id: tabsModel
        creator: creator
    }

    Component 
    {
        id: homeComponent
        Home {}
    }

    Component 
    {
        id: workspaceComponent
        Workspace {}
    }

}