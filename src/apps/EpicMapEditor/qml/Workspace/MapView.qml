import QtQuick
import QtQuick.Controls 2.15
import Game 1.0
import "MapView"

Rectangle
{
    property var assetsContext: null
    property var chapter: null
    property var hoveredCell: mouseArea.hoveredCell
    property bool showCoordinates: true
    property alias model: mapModel
    property alias isoView: isoView
    property alias toolsSelector: toolsSelector
    property alias renderItem: mapRenderItem

    // Play-test: emitted by the PlayControl overlay; Workspace forwards it
    // up to MainWindow, which opens (or restarts) the game tab.
    signal playRequested(var chapter, real camX, real camY, real camZoom)

    function load(path)
    {
        mapModel.load(path);
    }

    function save(path)
    {
        mapModel.save(path)
    }

    id: centerPanel


    SplitView.fillWidth: true
    color: "#ffffff"
    border.color: "#cccccc"
    clip: true

    MapModel
    {
        id: mapModel
    }

    AssetToolsSelector
    {
        id: toolsSelector

        currentAsset: assetsContext.asset
    }

    // Unified map rendering: the same sokol WorldRenderer the game client
    // uses, drawn into an FBO item. Camera stays on the shared isoView so
    // tools/RPC keep their single screenToMap source of truth.
    ModelFrameSource
    {
        id: modelSource
        mapModel: mapModel
        assetsLibrary: core.assetsLibrary
    }

    MapRenderItem
    {
        id: mapRenderItem
        anchors.fill: parent

        frameSource: modelSource

        cameraX: isoView.cameraX
        cameraY: isoView.cameraY
        cameraZoom: isoView.cameraZoom

        cursorCell: Qt.point(hoveredCell.x, hoveredCell.y)
        cursorFootprint: {
            var a = toolsSelector.currentAsset
            if (a && a.isBuilding3d)
                return Qt.point(a.footprintWidth, a.footprintHeight)
            return Qt.point(1, 1)
        }
        showGrid: true

        // Fence tool transient state (ghost preview + selection tint).
        fenceToolState: toolsSelector.fenceToolState
    }

    MapMouseArea
    {
        id: mouseArea
        anchors.fill: parent
        isoView: isoView
        onStrokeStart:(x, y, modifiers)=>
        {
            toolsSelector.stroke(0, Qt.point(x, y), mapModel, isoView,
                modifiers & Qt.ControlModifier, modifiers & Qt.ShiftModifier, modifiers & Qt.AltModifier)
        }
        onStrokeMove:(x, y, modifiers)=>
        {
            toolsSelector.stroke(1, Qt.point(x, y), mapModel, isoView,
                modifiers & Qt.ControlModifier, modifiers & Qt.ShiftModifier, modifiers & Qt.AltModifier)
        }
        onStrokeFinish:(x, y)=>
        {
            toolsSelector.stroke(2, Qt.point(x, y), mapModel, isoView, false, false, false)
        }
    }

    // Fence tool keys: Delete/Backspace erases the selected fence, Escape
    // clears the selection (forwarded to the active tool; others ignore it).
    Shortcut { sequences: ["Del", "Backspace"]; onActivated: toolsSelector.keyPress(Qt.Key_Delete, mapModel) }
    Shortcut { sequences: ["Esc"]; onActivated: toolsSelector.keyPress(Qt.Key_Escape, mapModel) }


    DiamondIsometryView
    {
        id: isoView
    }


    CoordinateIndicator
    {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 10
        width: 100
        height: 30
    }

    // Play-test: open/restart the game tab for this chapter (top-left overlay).
    PlayControl
    {
        id: playControl
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 10

        onPlayClicked: playRequested(chapter, isoView.cameraX, isoView.cameraY, isoView.cameraZoom)
    }

    Tools
    {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.topMargin: 10 + playControl.height + 10

        toolsModel: toolsSelector.toolsModel
        onToolClicked:(index) =>
        {
            toolsSelector.toolsModel.currentTool = index
        }
    }
}
