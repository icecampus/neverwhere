import QtQuick
import Game 1.0

Rectangle {
    color: colorPalette.surface     // Темная поверхность (#1A1A1A)
    border.color: colorPalette.border // Граница (#404040

    Text 
    {
        anchors.centerIn: parent
        text: "Right Panel"
        font.pixelSize: 16
        color: colorPalette.textPrimary
    }
}