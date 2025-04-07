import QtQuick

Item 
{
    signal clicked()
    property alias text: textItem.text
    property alias imageSource: image.source
    property color backroundColor: colorPalette.surface
    property color hoveredColor: colorPalette.primaryOrange
    property color borderColor: colorPalette.primaryOrange 
    property bool rounded: true
    property alias margin: content.anchors.margins

    id: rootGridElement

    Rectangle 
    {
        id: content
        anchors.fill: parent
        radius: (rounded) ? 10 : 0
        color: (mouseArea.containsMouse) ? hoveredColor : backroundColor    
        border.color: borderColor
        anchors.margins: 2
                
        Image 
        {
            id: image
            anchors.fill: parent
            anchors.margins: 5
            fillMode: Image.PreserveAspectFit 
        }

        Rectangle 
        {
            id: textBackground
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 5
            width: textItem.width + 20
            height: textItem.height + 10
            color: "#80000000" // Полупрозрачный черный (50% непрозрачности)
            radius: 4
            visible: (textItem.text)? true: false

            Text 
            {
                id: textItem
                anchors.centerIn: parent
                color: colorPalette.textPrimary
                font.pixelSize: 18
            }
        }
    }

    MouseArea 
    {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: 
        {
           rootGridElement.clicked()
        }
    }
}
