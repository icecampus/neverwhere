import QtQuick
import Qt.labs.qmlmodels
import Game 1.0
import "../GameObjects"

Item 
{
    property var isometryView: null
    property alias model: gameObjectsRepeater.model

    id: mapContainer
     
    transform: 
    [
        Scale 
        {
            xScale: isometryView.cameraZoom
            yScale: isometryView.cameraZoom
        },
        Translate 
        {
            x: isometryView.cameraX
            y: isometryView.cameraY
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
                roleValue: GameObjectTypes.Buildings
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
}
