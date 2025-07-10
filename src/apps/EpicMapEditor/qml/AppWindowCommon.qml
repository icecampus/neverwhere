import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.3
import Qt.labs.qmlmodels
import UI 1.0
import "UI"

Window 
{
    id: window

    visible: true
    x: 400
    y: 200
    width: 1920
    height: 1080
    title: "[3P1C|3D17ØR]"
    
    function maximaze()
	{
		if(window.visibility == Window.Windowed)
		{
			window.showMaximized()
		}
		else
		{
			window.showNormal()
		}
	}

    MainWindow
    {
        id: mainWindow
        anchors.fill: parent
    }  
}