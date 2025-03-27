import QtQuick 2.7
import Qt5Compat.GraphicalEffects

Rectangle
{
	property var iconSource: null
	property var iconColorOverlay: "white"
	property var hoveredColor: colorPalette.primaryOrange

	signal clicked()

	id: menuButton
	width: 45
	
	color: (mouseArea.containsMouse)? hoveredColor :"transparent"	

	Image
	{
		id: buttonIcon
		anchors.centerIn: parent
		width: 20
		height: 20
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
			menuButton.clicked();
		}
	}
}
