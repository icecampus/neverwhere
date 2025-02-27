import QtQuick
import QtQuick.Controls 2.15
import Game 1.0
import "Workspace"

 SplitView 
{
    orientation: Qt.Horizontal

    LeftPanel 
    {
        id: leftPanel
        SplitView.preferredWidth: 300
        SplitView.minimumWidth: 100
        SplitView.maximumWidth: 500
    }

    MapView 
    {
        id: centerPanel
        SplitView.fillWidth: true
        color: colorPalette.background  // Глубокий черный фон (#0A0A0A)
        border.color: colorPalette.border // Граница (#404040)
    }

    RightPanel {
        id: rightPanel
        SplitView.preferredWidth: 200
        SplitView.minimumWidth: 100
        SplitView.maximumWidth: 300
    }

    handle: Rectangle 
    {
        implicitWidth: 8
        color: SplitHandle.pressed ? colorPalette.darkOrange : colorPalette.surface2
    }
}