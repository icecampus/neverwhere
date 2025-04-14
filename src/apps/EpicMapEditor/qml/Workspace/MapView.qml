import QtQuick
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 
import Game 1.0
import "GameObjects"
import "MapView"

Rectangle 
{
    property var assetsContext: null
    property var hoveredCell: math.ivec2(1, 1)
    property bool showCoordinates: true

    id: centerPanel

    SplitView.fillWidth: true
    color: "#ffffff"
    border.color: "#cccccc"
    clip: true

    Component.onCompleted: 
    {
        mapModel.populateMapModel();
    }

    MapModel
    {
        id: mapModel
    }
    
    AssetToolsSelector
    {
        id: toolsSelector

        currentAsset: assetsContext.asset
    }

    StaggeredIsometryView
    {
        id: isoView
    }


    CustomItem 
    {
        property real t: 1
        center: Qt.point(-0.748, 0.1);
        iterationLimit: 3 * (zoom + 30)
        zoom: t * t / 10
        
        width: 20000
        height: 20000    

        transform: 
        [
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
        
        NumberAnimation on t {
            from: 1
            to: 60
            duration: 30*1000;
            running: true
            loops: Animation.Infinite
        }
        
    }
    
    Repeater
    {
        id: mapContainer
        model: mapModel

        GameObjectsContainer 
        {
            anchors.fill: parent        
        
            isometryView: isoView
            model: element
    
        }
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
        onClicked:(mouse)=>
        {
            if(mouse.button === Qt.LeftButton )
            {
                toolsSelector.click(Qt.point(mouse.x, mouse.y), mapModel, isoView)
            }
        }
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
