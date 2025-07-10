import QtQuick 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.3
import Qt.labs.qmlmodels
import UI 1.0
import "UI"

EpicEditorWindow  
{
    id: window
    property int previousX
    property int previousY

    visible: true
    x: 400
    y: 200
    width: 1920
    height: 1080
    title: "[3P1C|3D17ØR]" //3P1C M4P 3D170R | 3P1C_M4P_3D170R | [3P1C]M4P_3D170Я | [ 3P1C | M4P | 3D170R ] | [ 3P1C | M@P | 3D170R ] | ΞР1С М4Р ΞD!70Я

    
    caption: mainWindow.dragElement
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
        showWindowButtons: true
    }
}