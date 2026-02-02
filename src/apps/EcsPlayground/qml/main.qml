import QtQuick
import QtQuick.Controls

ApplicationWindow {
    visible: true
    width: 800
    height: 600
    title: "EnTT + QML Playground"

    // Background
    Rectangle {
        anchors.fill: parent
        color: "#2d2d2d"
    }

    GameView {
        anchors.fill: parent
        model: ecsModel
    }

    // Controls
    Row {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 10
        spacing: 10
        z: 100 // On top

        Button {
            text: "Add Entity"
            onClicked: ecsModel.addRandomEntity()
        }

        Button {
            text: "Clear"
            onClicked: ecsModel.clearEntities()
        }
        
        Label {
            text: "Count: " + ecsModel.rowCount()
            color: "white"
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    // Game View (Sokol)
    /*
    Item {
        id: gameArea
        anchors.fill: parent

        Repeater {
            id: view
            model: ecsModel

            delegate: Rectangle {
                x: model.posX
                y: model.posY
                width: 40
                height: 40
                radius: 20
                color: "cyan"
                border.color: "white"
                border.width: 2

                Text {
                    anchors.centerIn: parent
                    text: model.name
                    color: "black"
                    font.pixelSize: 10
                }

                Behavior on x { NumberAnimation { duration: 16 } } // Smooth interpolation
                Behavior on y { NumberAnimation { duration: 16 } }
            }
        }
    }
    */

    // Game Loop
    Timer {
        interval: 16 // ~60 FPS
        running: true
        repeat: true
        onTriggered: {
            ecsModel.tick(0.016)
        }
    }
}
