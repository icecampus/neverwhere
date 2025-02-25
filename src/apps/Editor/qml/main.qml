import QtQuick 2.15
import QtQuick.Controls 2.15
import "map"

ApplicationWindow 
{
    id: window
    visible: true
    width: 1920
    height: 1080
    title: "Isometric Grid"

    readonly property int tileWidth: 128
    readonly property int tileHeight: 64
    property int cameraX: -300
    property int cameraY: -200
    property real cameraZoom: 1.0

    /*
    Map
    {
        id: map
        anchors.fill: parent
    }
    */

color: darkPalette.background

    Palette
    {
        id:darkPalette
    }

      // Панель навигации
    Rectangle {
        width: parent.width
        height: 60
        color: darkPalette.surface
        border.color: darkPalette.border

        Text {
            text: "Dark App"
            color: darkPalette.primaryOrange
            font.bold: true
            font.pixelSize: 24
            anchors.centerIn: parent
        }
    }

    // Кнопка
    Rectangle {
        width: 120
        height: 40
        radius: 8
        color: mouseArea.containsMouse ? 
            darkPalette.lightOrange : darkPalette.primaryOrange
        anchors.centerIn: parent

        Text {
            text: "Action"
            color: darkPalette.surface
            anchors.centerIn: parent
            font.bold: true
        }

        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
        }
    }

    // Информационная карточка
    Rectangle {
        width: 280
        height: 160
        radius: 12
        color: darkPalette.surface2
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            margins: 20
        }

        border {
            width: 1
            color: darkPalette.border
        }

        Column {
            anchors {
                top: parent.top
                left: parent.left
                margins: 16
                right: parent.right
            }
            spacing: 8

            Text {
                text: "Statistics"
                color: darkPalette.textPrimary
                font.pixelSize: 16
                font.bold: true
            }

            Text {
                text: "Completed tasks: 12"
                color: darkPalette.textSecondary
                font.pixelSize: 14
            }

            Text {
                text: "Progress: 75%"
                color: darkPalette.primaryOrange
                font.pixelSize: 14
            }
        }
    }
}