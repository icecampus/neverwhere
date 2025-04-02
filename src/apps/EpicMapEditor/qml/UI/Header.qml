import QtQuick 
import QtQuick.Controls 
import Qt5Compat.GraphicalEffects


Rectangle
{
	property alias currentIndex: bar.currentIndex
	property alias dragElement: dragPanel
	property alias tabsModel: tabRepeater.model
	property alias bar: bar

	id: header
	height: 32
	color: colorPalette.background
    border.color: colorPalette.border
    border.width: 1
		

	MainFramelessButton
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
		
	//
    TabBar 
	{
        id: bar
        anchors.top: parent.top
        anchors.left: menuButton.right
        height: header.height
        spacing: 1

        background: Rectangle 
		{
            color: colorPalette.darkOrange
        }
		Repeater
		{
			id: tabRepeater
			MainFramelessTab 
			{
				id: firstTab
				text: element.name
				width: 100
				height: 50
			}
		}

		Component
		{
			id: newTabComponent
			MainFramelessTab
			{
				width: 220
				height: bar.height
				closable: true

				Component.onCompleted:
				{
					bar.width+= width + 1
				}
			
				onCloseClicked:
				{
					//window.removeTab(TabBar.index);
				}

				onPinnedClicked:
				{
					//createNewWindow(TabBar.index);
				}
			}
		}

		function createNewTab(name)
		{
			var newTab = newTabComponent.createObject(bar); 
			newTab.text = name;
			bar.addItem(newTab);
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

		MainFramelessButton
		{
			id: minButton
			height: parent.height
			onClicked:
			{
				window.showMinimized();
			}
			iconSource:"qrc:/resources/icons/minimize.png"
		}
			
		MainFramelessButton
		{
			id: maxButton
			height: parent.height

			onClicked:
			{
				maximaze();
			}
			iconSource:"qrc:/resources/icons/maximaze.png"
		}

		MainFramelessButton
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