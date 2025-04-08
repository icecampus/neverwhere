import QtQuick
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 
import Game 1.0
import "Tools"


Item 
{
    id: coordinateDisplay
    width:  50
    height: toolsRow.height + 20
        
    Rectangle 
    {
        id: background
        anchors.fill: parent
        color: colorPalette.surface2 
        opacity: 0.7 
        radius: 5
    }

    Column
    {
        id: toolsRow
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        Repeater
        {
            model: core.tools.toolsModel
            
            ToolButton
            {
                anchors.horizontalCenter: parent.horizontalCenter
		        width: 44
		        height: 36
                
                selected: index === core.tools.toolsModel.currentTool
                image: element.icon

                onClicked:
                {
                    core.tools.toolsModel.currentTool = index
                }
            }

        }
        
    }
        
   
}