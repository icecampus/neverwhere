import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ApplicationWindow {
    visible: true
    width: 1024
    height: 768
    title: "RTTR + EnTT Editor"

    property int selectedIndex: -1
    property var inspectorData: []

    function updateInspector() {
        if (selectedIndex >= 0) {
            inspectorData = gameModel.getInspectorData(selectedIndex)
        } else {
            inspectorData = []
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Game Area
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#333"
            clip: true

            Repeater {
                model: gameModel
                delegate: Rectangle {
                    x: model.posX
                    y: model.posY
                    width: 60
                    height: 60
                    color: index === selectedIndex ? "yellow" : "lightblue"
                    border.color: "white"
                    radius: 5
                    
                    Text {
                        anchors.centerIn: parent
                        text: model.display
                        font.pixelSize: 10
                        wrapMode: Text.Wrap
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                    }

                    MouseArea {
                        anchors.fill: parent
                        drag.target: parent
                        
                        onPressed: {
                            selectedIndex = index
                            updateInspector()
                        }

                        // Sync back x/y to model on release
                        onReleased: {
                            gameModel.setProperty(index, "TransformComponent", "x", parent.x)
                            gameModel.setProperty(index, "TransformComponent", "y", parent.y)
                            updateInspector()
                        }
                    }
                }
            }
            
            Button {
                text: "Add Random Entity"
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.margins: 20
                onClicked: gameModel.addRandomEntity()
            }
        }

        // Property Grid
        Rectangle {
            Layout.preferredWidth: 350
            Layout.fillHeight: true
            color: "#222"
            
            ColumnLayout {
                anchors.fill: parent
                spacing: 10

                Text {
                    text: "Inspector"
                    color: "white"
                    font.bold: true
                    font.pixelSize: 18
                    Layout.alignment: Qt.AlignHCenter
                    Layout.topMargin: 10
                }
                
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    
                    ListView {
                        width: parent.width
                        model: inspectorData
                        
                        delegate: Column {
                            width: parent.width
                            spacing: 5
                            
                            Rectangle {
                                width: parent.width
                                height: 30
                                color: "#444"
                                Text {
                                    text: modelData.name
                                    color: "white"
                                    anchors.centerIn: parent
                                    font.bold: true
                                }
                            }
                            
                            Repeater {
                                property string compName: modelData.name
                                model: modelData.properties
                                delegate: RowLayout {
                                    width: parent.width
                                    spacing: 10
                                    
                                    Text {
                                        text: modelData.name
                                        color: "#ccc"
                                        Layout.preferredWidth: 80
                                        Layout.leftMargin: 10
                                    }
                                    
                                    TextField {
                                        Layout.fillWidth: true
                                        Layout.rightMargin: 10
                                        text: modelData.value
                                        color: "black"
                                        background: Rectangle { color: "white" }
                                        
                                        onEditingFinished: {
                                            var val = text
                                            // Simple type conversion based on typeName
                                            if (modelData.typeName === "float" || modelData.typeName === "double") val = parseFloat(text)
                                            else if (modelData.typeName === "int") val = parseInt(text)
                                            // bool handling could be added
                                            
                                            // parent.compName is from outer Repeater
                                            var componentName = parent.parent.compName
                                            gameModel.setProperty(selectedIndex, componentName, modelData.name, val)
                                            
                                            // Force update to reflect normalized values
                                            updateInspector()
                                        }
                                    }
                                }
                            }
                            
                            Item { height: 10; width: 1 } // Spacer
                        }
                    }
                }
            }
        }
    }
}
