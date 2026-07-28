import QtQuick
import Game 1.0
import "AssetSettings"
import "../../Common"

// Per-asset settings block of the right panel. Appears only while the asset
// is in editMode (the gear button on the palette tile sets asset + editMode
// together; save/cancel here or on the tile leave edit mode). Cliff3d assets
// additionally get the whole cliff generator params via the embedded
// CliffSettings; for other asset types that part collapses to zero height.
Rectangle 
{
    property var asset: null
    property alias text: titleText.text
    // Driven by RightPanel: the selected asset is in edit mode.
    property bool activeAsset: false

    id: assetSettings
    color: colorPalette.surface2     
    border.color: colorPalette.border 
    radius: 5 

    visible: activeAsset
    height: activeAsset ? title.height + content.height : 0
    
    Rectangle
    {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        height: 30
        color: "#80000000"
        radius: 5 

        Text
        {
            id: titleText     
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 5

            color: colorPalette.textPrimary
            font.pixelSize: 16
        }
    }
    
    Column 
    {
        id: content
        anchors.top: title.bottom
        anchors.left: parent.left
        anchors.right: parent.right
        
        anchors.leftMargin: 5
        anchors.rightMargin: 5
        anchors.topMargin: 5

        spacing: 5
        Row
        {
            height: 40
            width: parent.width
            Item{ height: 30; width: parent.width/2;  Text{ text: "Name: ";  anchors.verticalCenter: parent.verticalCenter;  color: colorPalette.textPrimary; font.pixelSize: 16 }} 
            Text{ width: parent.width/2; text: (asset)? asset.name : ""; anchors.verticalCenter: parent.verticalCenter;  color: colorPalette.textPrimary; font.pixelSize: 16 }
        }

        // Cliff3d generator/shading params (embedded without its own title
        // and save button — this block already provides both).
        CliffSettings
        {
            width: parent.width
            asset: assetSettings.asset
            activeAsset: assetSettings.asset && assetSettings.asset.isCliff3d
            showTitle: false
            showSaveButton: false
            color: "transparent"
            border.color: "transparent"
        }

        // Cyclopean3d composer params (same embedding contract as CliffSettings).
        CyclopeanSettings
        {
            width: parent.width
            asset: assetSettings.asset
            activeAsset: assetSettings.asset && assetSettings.asset.isCyclopean3d
            showTitle: false
            showSaveButton: false
            color: "transparent"
            border.color: "transparent"
        }

        Row
        { 
            anchors.horizontalCenter: parent.horizontalCenter
            height: 40

            ImageButton
            {
                width: 40
                height: 40
                imageSource: "qrc:/resources/icons/cancel.png"
                onClicked: if (assetSettings.asset) assetSettings.asset.editMode = false
            }
            
            ImageButton
            {
                width: 40
                height: 40
                imageSource: "qrc:/resources/icons/save.png"
                onClicked:
                {
                    if (!assetSettings.asset)
                        return
                    core.assetsLibrary.save(assetSettings.asset)
                    assetSettings.asset.editMode = false
                }
            }
        }

       
        Item
        {
            width: parent.width
            height:5
        }

    }

}
