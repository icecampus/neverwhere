import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 

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

    Palette
    {
        id:vibrantDarkPalette
    }
    /*
    Map
    {
        id: map
        anchors.fill: parent
    }
    */

    color: vibrantDarkPalette.background

    // Панель навигации с градиентом
    Rectangle {
        width: parent.width
        height: 80
        gradient: Gradient {
            GradientStop { position: 0.0; color: vibrantDarkPalette.surface }
            GradientStop { position: 1.0; color: vibrantDarkPalette.surface2 }
        }

        Text {
            text: "VIBRANT UI"
            color: vibrantDarkPalette.primaryOrange
            font {
                pixelSize: 28
                bold: true
                letterSpacing: 2
            }
            anchors.centerIn: parent
        }
    }

    // Активная кнопка с эффектом свечения

    Rectangle {
        width: 160
        height: 50
        radius: 8
        color: vibrantDarkPalette.primaryOrange
        anchors.centerIn: parent

        layer.enabled: true
        layer.effect: Glow {
            color: "#80FF9100"
            radius: 16
            samples: 25
        }

        Text {
            text: "SUBMIT"
            color: vibrantDarkPalette.background
            font {
                pixelSize: 16
                bold: true
            }
            anchors.centerIn: parent
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onEntered: parent.color = vibrantDarkPalette.lightOrange
            onExited: parent.color = vibrantDarkPalette.primaryOrange
        }
    }

    // Карточка с акцентами
    Rectangle {
        width: 300
        height: 180
        radius: 12
        color: vibrantDarkPalette.surface
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            margins: 30
        }

        border {
            width: 1
            color: vibrantDarkPalette.primaryOrange
        }

        Column {
            anchors {
                top: parent.top
                left: parent.left
                margins: 20
                right: parent.right
            }
            spacing: 12

            Text {
                text: "🔥 Active Items"
                color: vibrantDarkPalette.textPrimary
                font.pixelSize: 18
            }

            Rectangle {
                width: parent.width - 40
                height: 4
                radius: 2
                color: vibrantDarkPalette.surface2
                
                Rectangle {
                    width: parent.width * 0.75
                    height: parent.height
                    radius: 2
                    color: vibrantDarkPalette.primaryOrange
                }
            }

            Text {
                text: "● 3 new notifications"
                color: vibrantDarkPalette.lightOrange
                font.pixelSize: 14
            }
        }
    }
}