import QtQuick
import QtQuick.Controls 2.15
import Game 1.0
import UI 1.0

Item
{
    property alias tabsModel: tabsModel
    property var contentParent: null
    property var bar: null

    function openChapterByUuid(name, uuid) 
    {
        createOrActivateTab(name, TabType.Workspace, { chapterUuid: uuid }, {})
    }

    // Play-test: open the game tab for a chapter, or restart it if already
    // open. One tab per chapter — dedup is by tab name ("▶ " + name).
    function playChapter(name, uuid, camX, camY, camZoom)
    {
        var tabName = "▶ " + name
        var data = { chapterUuid: uuid }
        var extra = { nonce: Date.now(), camX: camX, camY: camY, camZoom: camZoom }

        createOrActivateTab(tabName, TabType.Game, data, extra)

        // Bring the game tab forward (createOrActivateTab switches only on creation).
        var index = tabsModel.getIndexByName(tabName)
        if (index >= 0)
        {
            bar.currentIndex = index
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
            var fixed = type==TabType.Home
            var result = tabsModel.add(type, name, fixed, data, extraData)
            if (result) 
            {
                bar.currentIndex = bar.count - 1
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
            case TabType.Game:
                return gameComponent
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

    Component
    {
        id: gameComponent
        GameTab {}
    }

    /*
    component MyTabButton: TabButton 
    {
    }
    */

}