import QtQuick
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
        delegate: GameObject 
        {
            gameObject: element
        }
    }
}
