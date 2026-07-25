import QtQuick
import QtQuick.Controls 2.15
import Game 1.0
import "Workspace"

// Workspace tab root. The SplitView owns the three visual panels; the
// non-visual AssetsContext must NOT be a SplitView child (a plain QObject in
// a SplitView breaks its layout bookkeeping — the right panel collapses).
Item
{
    id: workspaceRoot

    property var chapter: null

    // Play-test: forwarded from MapView's PlayControl up to MainWindow.
    signal playRequested(var chapter, real camX, real camY, real camZoom)

    function load(data)
    {
        chapter = core.chapters.getByUuid(data.chapterUuid)
        mapView.load(chapter.mapPath)
        // Register the active scene so the RPC server can reach it.
        sceneRegistry.setActiveScene(mapView.model, mapView.isoView, mapView.toolsSelector, chapter, mapView.renderItem, assetsContext)
    }

    anchors.fill: parent

    AssetsContext
    {
        id: assetsContext
        assetsLibrary: core.assetsLibrary
    }

    SplitView
    {
        id: workspace
        anchors.fill: parent
        orientation: Qt.Horizontal

        // Play-test: forwarded from MapView's PlayControl up to MainWindow.
        // (the signal lives on workspaceRoot — MainWindow connects there)

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
            chapter: workspaceRoot.chapter

            onPlayRequested: (ch, camX, camY, camZoom) =>
            {
                // MUST be qualified: MapView has its own playRequested signal, and
                // an unqualified call re-emits THAT (infinite signal loop, the
                // click dies there). We need the Workspace-level signal instead.
                workspaceRoot.playRequested(ch, camX, camY, camZoom)
            }
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
}