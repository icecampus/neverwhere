import QtQuick 2.7
import Qt5Compat.GraphicalEffects

Rectangle
{
	property var iconSource: null
	property var iconColorOverlay: "white"
	property var hoveredColor: colorPalette.primaryOrange
	property alias containsMouse: mouseArea.containsMouse

	signal clicked()

	id: tabButton
	radius: 4

	color:  ( mouseArea.pressed ) ?  colorPalette.darkOrange : hoveredColor 

	Image
	{
		id: buttonIcon
		anchors.centerIn: parent
		width: 16
		height: 16
		visible: false
		source: iconSource ? iconSource : ""
	}

	ColorOverlay {
        anchors.fill: buttonIcon
        source: buttonIcon
        color: iconColorOverlay
		visible: iconSource != null
    }

	
	MouseArea
	{
		id: mouseArea
		anchors.fill: parent
		hoverEnabled: true		

		onClicked:
		{
			tabButton.clicked();
		}
	}
	
}
