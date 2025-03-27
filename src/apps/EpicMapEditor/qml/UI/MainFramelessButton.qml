import QtQuick 
import Qt5Compat.GraphicalEffects

Rectangle
{
	property var iconSource: null
	property var iconColorOverlay: "red"
	property var hoveredColor: "pink"

	signal clicked()

	id: menuButton
	width: 45
	
	color: (mouseArea.containsMouse)? hoveredColor :"transparent"	

	Image
	{
		id: buttonIcon
		anchors.centerIn: parent
		width: 10
		height: 10
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
