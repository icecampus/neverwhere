import QtQuick 
import QtQuick.Controls 
import Qt5Compat.GraphicalEffects


Rectangle
{
	property alias currentIndex: bar.currentIndex
	property alias dragElement: dragPanel
	property alias tabsModel: bar.model
	property alias bar: bar

	signal closeRequest(int index)

	id: header
	height: 32
	color: colorPalette.background
    border.color: colorPalette.border
    border.width: 1

	HeaderButton
	{
		id: menuButton
		anchors.left: parent.left
		anchors.top: parent.top
		anchors.bottom: parent.bottom
			
		Column
		{
			width: 15
			anchors.verticalCenter: parent.verticalCenter
			anchors.horizontalCenter: parent.horizontalCenter
			spacing:5
			Rectangle{ width: 15; height:1;}
			Rectangle{ width: 15; height:1;}
			Rectangle{ width: 15; height:1;}
		}
	}

	ListView 
	{
        id: bar
        anchors.top: parent.top
        anchors.left: menuButton.right
        height: header.height
		width: Math.min(contentWidth, header.width - 50)

		property int currentIndex: 0

		orientation: ListView.Horizontal
		
		clip: true
		interactive: false

		delegate: HeaderTab 
		{
			height: header.height
            text: element.name
			checked: currentIndex === index
			onClicked: currentIndex = index
			closable: !element.fixed
			onCloseClicked:
			{
				header.closeRequest(index)
			}
        }
    }
		
    Rectangle 
	{
        id: dragPanel
        anchors.top: parent.top
        anchors.left:  bar.right
        anchors.right: windowButton.left
        anchors.bottom: parent.bottom
        color: "transparent"
    }

	//header buttons
	Row
	{
		id: windowButton
		anchors.top: parent.top
		anchors.right:  parent.right
		anchors.bottom: parent.bottom

		spacing: 2

		HeaderButton
		{
			id: minButton
			height: parent.height
			onClicked:
			{
				window.showMinimized();
			}
			iconSource:"qrc:/resources/icons/minimize.png"
		}
			
		HeaderButton
		{
			id: maxButton
			height: parent.height

			onClicked:
			{
				maximaze();
			}
			iconSource:"qrc:/resources/icons/maximaze.png"
		}

		HeaderButton
		{
			id: closeButton
			height: parent.height
			hoveredColor: colorPalette.error

			onClicked:
			{
				window.close();
			}
			iconSource:"qrc:/resources/icons/close.png"
		}
	}

    // Горизонтальный разделитель
    Rectangle {
        anchors.bottom: parent.bottom
        width: parent.width
        height: 1
        color: colorPalette.border
    }

    // Анимация переключения вкладок
    Behavior on color {
        ColorAnimation { duration: 150 }
    }
}