import QtQuick
import QtQuick.Controls 2.15
import Game 1.0
import "Workspace"

 SplitView 
{
    property var chapter: null

    function load(data)
    {
        chapter = core.chapters.getByUuid(data.chapterUuid)
        mapView.load(chapter.mapPath)
        // Register the active scene so the RPC server can reach it.
        sceneRegistry.setActiveScene(mapView.model, mapView.isoView, mapView.toolsSelector, chapter)
    }

    orientation: Qt.Horizontal

    AssetsContext
    {
        id: assetsContext
        assetsLibrary: core.assetsLibrary
    }

    LeftPanel 
    {
        id: leftPanel
        SplitView.preferredWidth: 420
        SplitView.minimumWidth: 200
        SplitView.maximumWidth: 600
        
        assetsContext: assetsContext
        onSave:
        {
            mapView.save(chapter.mapPath)
        }
    }

    MapView 
    {
        id: mapView
        SplitView.fillWidth: true
        color: colorPalette.background  // Глубокий черный фон (#0A0A0A)
        border.color: colorPalette.border // Граница (#404040)

        assetsContext: assetsContext
    }

    RightPanel 
    {
        id: rightPanel
        SplitView.preferredWidth: 250
        SplitView.minimumWidth: 100
        SplitView.maximumWidth: 350

        currentAsset: assetsContext.asset
        mapModel: mapView.model
    }

    handle: Rectangle 
    {
        implicitWidth: 3
        color: SplitHandle.pressed ? colorPalette.darkOrange : colorPalette.surface2
    }
}