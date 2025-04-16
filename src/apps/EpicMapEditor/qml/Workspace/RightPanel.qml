import QtQuick
import Qt.labs.qmlmodels
import Game 1.0
import "PropertiesContainers"

Rectangle 
{
    property var asset: null

    id: rightPanel
    color: colorPalette.surface     
    border.color: colorPalette.border 

       
    PropertyContainersModel
    {
        id: containersModel
    }

    ListView
    {
        id: conteinersView
        anchors.fill: parent
        anchors.margins: 5

        model: containersModel
        spacing: 5
        clip: true

        delegate: DelegateChooser
        {
            id: chooser
            role: "type"

            DelegateChoice 
            {   
                roleValue: PropertiesContainerTypes.MapSettings
                delegate: MapSettings
                {
                    width: conteinersView.width
                    height: 100
                    text: element.title
                }
            }
            DelegateChoice 
            {   
                roleValue: PropertiesContainerTypes.AssetSettings
                delegate: AssetSettings
                {
                    width: conteinersView.width
                    height: 200
                    text: element.title
                    asset: rightPanel.asset
                }
            }

        }

    }
    


}