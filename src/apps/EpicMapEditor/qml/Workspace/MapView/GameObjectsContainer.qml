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
                roleValue: AssetTypes.image
                delegate: ImageGOView
                {
                    gameObject: element
                }
            }
        }
    }
}
