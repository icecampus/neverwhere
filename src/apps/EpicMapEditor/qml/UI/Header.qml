import QtQuick 2.7
import QtQuick.Controls 2.3


Rectangle
{
	property alias currentIndex: bar.currentIndex
	property alias dragElement: dragPanel

	id: header
	height: 30
	color: "blue"
		
	Component.onCompleted:
	{
		window.loadSetting();
		if(window.isNeedMaximized())
		{
			showMaximized();
		}
	}

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
		width: firstTab.width //Todo: fix me

		background: Rectangle
		{
			color: "red"
		}

		MainFramelessTab
		{
			id: firstTab
			text: qsTr("Home")
			width: 100
			height: bar.height
			//verticalCenter: parent.verticalCenter
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
		color: "yellow"
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
			//iconSource:"qrc:/icons/images/window-minimize.png"
		}
			
		MainFramelessButton
		{
			id: maxButton
			height: parent.height

			onClicked:
			{
				maximaze();
			}
			//iconSource:(window.visibility == Window.Windowed)?"qrc:/icons/images/window-windowed-mode.png":"qrc:/icons/images/window-fullscreen.png"
		}

		MainFramelessButton
		{
			id: closeButton
			height: parent.height
			hoveredColor: "fuchsia"

			onClicked:
			{
				window.close();
			}
			//iconSource:"qrc:/icons/images/window-close.png"
		}
	}
}