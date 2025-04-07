import QtQuick 
import QtQuick.Controls
import Qt5Compat.GraphicalEffects

Rectangle
{
	id: control
	property bool closable: false
	property bool checked: false
	property alias text: label.text
	
	signal clicked()
	signal pinnedClicked()
	signal closeClicked()

	height: parent.height
	width: label.implicitWidth + 30

    color: (control.checked) ? colorPalette.neonOrange : 
		   (mouseArea.pressed) ? colorPalette.lightOrange : 		
		   (mouseArea.containsMouse) ? colorPalette.primaryOrange : colorPalette.background

	Behavior on color {
        ColorAnimation { duration: 100 }
    }

	Item
	{	
		anchors.left: parent.left
		anchors.right: parent.right
		height: parent.height
		Label 
		{
			id: label
			anchors.centerIn: parent
			text: parent.text           
			color: (control.checked || mouseArea.containsMouse)? colorPalette.textPrimary: "grey"          
			font.pixelSize: 18          
			horizontalAlignment: Text.AlignHCenter  
			verticalAlignment: Text.AlignVCenter    
		}
	}

	MouseArea
	{
		id: mouseArea
		anchors.fill: parent
		hoverEnabled: true		
		onClicked:
		{
			control.clicked()
		}
	}

	Rectangle 
	{
		id: underButton
		anchors.right: parent.right
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		width: control.height/1.5 + 5
		color: control.color
		visible: closeButton.visible
	
	}
	
	Rectangle 
	{
		anchors.right: underButton.left
		anchors.top: parent.top
		anchors.bottom: parent.bottom
		width: 20
		visible: closeButton.visible

		layer.enabled: true
		layer.effect: LinearGradient 
		{
			start: Qt.point(width, 0)    
			end: Qt.point(0, 0)      
			gradient: Gradient 
			{
				GradientStop { position: 0.0; color: control.color }
				GradientStop { position: 1.0; color: "transparent" }
			}
    }
}

	
	HeaderTabButton
	{
		id: closeButton

		anchors.right:  parent.right
		anchors.rightMargin:  4
		anchors.verticalCenter: parent.verticalCenter

		width: control.height/1.7 
		height: control.height/1.7
		visible: closable && (mouseArea.containsMouse || containsMouse)

		onClicked:
		{
			closeClicked();
		}
		iconSource: "qrc:/resources/icons/close.png"
	}
	
}
