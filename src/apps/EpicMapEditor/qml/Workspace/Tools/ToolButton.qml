import QtQuick
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 
import Game 1.0


Rectangle
{
    property alias image: icon.source
    property bool selected: false

    signal clicked()

    id: toolButton
    border.color: selected ? colorPalette.primaryOrange : "transparent"

    color:  ( mouseArea.pressed ) ?  colorPalette.darkOrange: 
			( mouseArea.containsMouse ) ? colorPalette.primaryOrange : colorPalette.surface2
    
  
    radius: 4

    Image
    {
        id: icon
        anchors.centerIn: parent
    
       	width: 32
    	height: 32

        source: element.icon
    }

    MouseArea
	{
		id: mouseArea
		anchors.fill: parent
		hoverEnabled: true		

		onClicked:
		{
			toolButton.clicked();
		}
	}
}
