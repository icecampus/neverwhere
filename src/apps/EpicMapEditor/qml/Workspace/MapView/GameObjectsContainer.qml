import QtQuick
import Qt.labs.qmlmodels
import Game 1.0
import "../GameObjects"

Item 
{
    property alias model: gameObjectsRepeater.model
    property var isoView: null

    id: mapContainer
                
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

    Repeater 
    {
        id: gameObjectsRepeater
        DelegateChooser 
        {
            id: chooser
            role: "type"

            DelegateChoice 
            {   
                roleValue: GameObjectTypes.Tile2D
                delegate: Tile2DView
                {
                    gameObject: element
                }
            }
            DelegateChoice 
            {   
                roleValue: GameObjectTypes.Landscape
                delegate: LandscapeView
                {
                    gameObject: element
                }
            }
        }
    }


    CustomItem {
        property real t: 1
        anchors.fill: parent
        center: Qt.point(-0.748, 0.1);
        iterationLimit: 3 * (zoom + 30)
        zoom: t * t / 10
        
        NumberAnimation on t {
            from: 1
            to: 60
            duration: 30*1000;
            running: true
            loops: Animation.Infinite
        }
        
    }
}
