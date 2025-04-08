import QtQuick
import QtQuick.Controls 2.15
import Game 1.0
import "Workspace"

 SplitView 
{
    orientation: Qt.Horizontal

    AssetsContext
    {
        id: assetsContext
        assetsLibrary: core.assetsLibrary
    }

    LeftPanel 
    {
        id: leftPanel
        SplitView.preferredWidth: 370
        SplitView.minimumWidth: 100
        SplitView.maximumWidth: 500
        
        assetsContext: assetsContext
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
        implicitWidth: 3
        color: SplitHandle.pressed ? colorPalette.darkOrange : colorPalette.surface2
    }
}