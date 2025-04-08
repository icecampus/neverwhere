import QtQuick
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 
import Game 1.0
import "GameObjects"
import "MapView"

Rectangle 
{
    id: centerPanel
    SplitView.fillWidth: true
    color: "#ffffff"
    border.color: "#cccccc"
    clip: true

    property var hoveredCell: math.ivec2(1, 1)
    property bool showCoordinates: true

    Component.onCompleted: 
    {
        mapModel.populateMapModel();
    }

    MapModel
    {
        id: mapModel
    }

    StaggeredIsometryView
    {
        id: isoView
    }
    
    StaggeredGrid 
    {
        id: customItem
        topology: isoView
        size: Qt.size(200, 200)
        
        color: "grey"

        transform: [
            Scale 
            {
                xScale: isoView.cameraZoom
                yScale: isoView.cameraZoom
            },
            Translate 
            {
                x: isoView.cameraX
                y: isoView.cameraY
            }
        ]
    }

    GameObjectsContainer 
    {
        id: mapContainer
        anchors.fill: parent        
        
        isoView: isoView
        model: mapModel
    
    }
    
    StaggeredCursor
    {
        topology: isoView
        color: "red"
        mapPosition: hoveredCell

        transform: [
            Scale {
                xScale: isoView.cameraZoom
                yScale: isoView.cameraZoom
            },
            Translate {
                x: isoView.cameraX
                y: isoView.cameraY
            }
        ]
    }
    
    MapMouseArea 
    {
        id: mouseArea
        anchors.fill: parent
        isoView: isoView
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
        
        toolsModel: core.tools.toolsModel
        onToolClicked:(index) =>
        {
            core.tools.toolsModel.currentTool = index
        }
    }
}
