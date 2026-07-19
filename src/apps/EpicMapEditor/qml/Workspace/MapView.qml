import QtQuick
import QtQuick.Controls 2.15
import Game 1.0
import "MapView"

Rectangle
{
    property var assetsContext: null
    property var hoveredCell: math.ivec2(1, 1)
    property bool showCoordinates: true
    property alias model: mapModel
    property alias isoView: isoView
    property alias toolsSelector: toolsSelector

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
    MapRenderItem
    {
        id: mapRenderItem
        anchors.fill: parent

        mapModel: mapModel
        assetsLibrary: core.assetsLibrary

        cameraX: isoView.cameraX
        cameraY: isoView.cameraY
        cameraZoom: isoView.cameraZoom

        cursorCell: Qt.point(hoveredCell.x, hoveredCell.y)
        showGrid: true
    }

    MapMouseArea
    {
        id: mouseArea
        anchors.fill: parent
        isoView: isoView
        onClicked:(mouse)=>
        {
            if(mouse.button === Qt.LeftButton )
            {
                toolsSelector.click(Qt.point(mouse.x, mouse.y), mapModel, isoView,
                    mouse.modifiers & Qt.ControlModifier, mouse.modifiers & Qt.ShiftModifier, mouse.modifiers & Qt.AltModifier)
            }
        }
    }


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

    Tools
    {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 10

        toolsModel: toolsSelector.toolsModel
        onToolClicked:(index) =>
        {
            toolsSelector.toolsModel.currentTool = index
        }
    }
}
