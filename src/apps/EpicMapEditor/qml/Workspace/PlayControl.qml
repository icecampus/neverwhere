import QtQuick

// Small overlay button that starts (or restarts) the play-test tab for the
// current chapter. Same visual pattern as CoordinateIndicator: translucent
// rounded plate in the map corner.
Item
{
    signal playClicked()

    id: playControl

    width: 40
    height: 30

    Rectangle
    {
        anchors.fill: parent
        color: colorPalette.surface2
        opacity: playMouseArea.containsMouse ? 0.95 : 0.7
        radius: 5
    }

    Text
    {
        anchors.centerIn: parent
        text: "▶"
        color: "white"
        font.pixelSize: 14
    }

    MouseArea
    {
        id: playMouseArea
        anchors.fill: parent
        hoverEnabled: true
        onClicked: playControl.playClicked()
    }
}
