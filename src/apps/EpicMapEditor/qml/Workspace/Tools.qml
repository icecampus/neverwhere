import QtQuick
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 
import Game 1.0


Item 
{
    id: coordinateDisplay
    width:  toolsRow.width + 20
    height: 46
        
    Rectangle 
    {
        id: background
        anchors.fill: parent
        color: colorPalette.surface2 
        opacity: 0.7 
        radius: 5
    }

    Row
    {
        id: toolsRow
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Repeater
        {
            model: core.tools.toolsModel
            
            Rectangle
            {
                anchors.verticalCenter: parent.verticalCenter
		        width: 44
		        height: 36
                border.color: colorPalette.primaryOrange 
                color: colorPalette.surface2
                radius: 5

                Image
                {
                    id: icon
                    anchors.centerIn: parent
    
       		        width: 32
    		        height: 32

                    source: element.icon
                }
            }

        }
        
    }
        
   
}