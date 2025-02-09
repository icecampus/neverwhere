import QtQuick
import QtQuick.Controls

Window {
    width: 400
    height: 300
    visible: true
    title: "Qt6 QML Demo"

    Rectangle {
        anchors.centerIn: parent
        width: 200
        height: 100
        color: "lightblue"
        radius: 10
        
        Text {
            anchors.centerIn: parent
            text: "Hello Qt6 QML!"
            font.pixelSize: 20
        }
    }
}