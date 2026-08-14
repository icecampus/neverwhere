import QtQuick
import Game 1.0
import QtQuick.Controls 2.15

// Settings panel for building3d assets: the placement parameter set of the
// GLB bundle (cell footprint, uniform scale on top of the footprint fit,
// yaw, height scale), edited live (the renderer reloads the mesh on the next
// frame sync via ensureBuildingAsset's param compare). Save persists the
// payload to the asset's index.json.
// Hosted inside the AssetSettings right-panel block (asset in editMode) with
// its own title/save hidden; can also stand alone (showTitle/showSaveButton).
Rectangle
{
    property var asset: null
    property alias text: titleText.text
    // Set by the host: the panel content only shows for building3d assets.
    property bool activeAsset: false
    // Standalone-hosting knobs (off when embedded into AssetSettings).
    property bool showTitle: true
    property bool showSaveButton: true

    id: buildingSettings
    color: colorPalette.surface2
    border.color: colorPalette.border
    radius: 5

    visible: activeAsset
    height: activeAsset ? (title.height + content.height + 10) : 0

    // Re-read when the selected asset changes; rows also read through `params`.
    // Same guard as MaskSettings: plain assets have no buildingParams()
    // invokable, and binding through activeAsset would race on asset switch.
    property var params: (asset && asset.isBuilding3d) ? asset.buildingParams() : ({})

    // [key, label, from, to, stepSize]
    property var rows: [
        ["footprintWidth", "Footprint width (cells)", 1.0, 9.0, 1.0],
        ["footprintHeight", "Footprint height (cells)", 1.0, 9.0, 1.0],
        ["scale", "Scale", 0.25, 2.0, 0.05],
        ["yawDegrees", "Yaw (degrees)", -180.0, 180.0, 5.0],
        ["heightScale", "Height scale (px)", 32.0, 192.0, 1.0]
    ]

    // [key, label] — descriptor file links, shown read-only.
    property var fileRows: [
        ["model", "Model (.glb)"],
        ["albedo", "Albedo"],
        ["thumbnail", "Thumbnail"]
    ]

    component ParamRow: Row
    {
        property string key: ""
        property string label: ""
        property real from: 0.0
        property real to: 1.0
        property real stepSize: 0.01

        width: parent ? parent.width : 0
        height: 26
        spacing: 5

        Text
        {
            width: parent.width * 0.38
            text: label
            color: colorPalette.textPrimary
            font.pixelSize: 12
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
        Slider
        {
            id: slider
            width: parent.width * 0.42
            from: parent.from
            to: parent.to
            stepSize: parent.stepSize
            value: buildingSettings.params[key] !== undefined ? buildingSettings.params[key] : 0
            anchors.verticalCenter: parent.verticalCenter
            onMoved: if (buildingSettings.asset) buildingSettings.asset.setBuildingParam(key, value)
        }
        Text
        {
            width: parent.width * 0.16
            text: Number(slider.value).toFixed(stepSize >= 1.0 ? 0 : 2)
            color: colorPalette.textPrimary
            font.pixelSize: 11
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    component FileRow: Row
    {
        property string key: ""
        property string label: ""

        width: parent ? parent.width : 0
        height: 22
        spacing: 5

        Text
        {
            width: parent.width * 0.38
            text: label
            color: colorPalette.textPrimary
            font.pixelSize: 12
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
        Text
        {
            width: parent.width * 0.58
            text: buildingSettings.params[key] !== undefined ? buildingSettings.params[key] : ""
            color: colorPalette.textSecondary
            font.pixelSize: 11
            anchors.verticalCenter: parent.verticalCenter
            elide: Text.ElideRight
        }
    }

    Rectangle
    {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: buildingSettings.showTitle
        height: buildingSettings.showTitle ? 30 : 0
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
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 5
        spacing: 4

        Repeater
        {
            model: buildingSettings.rows
            delegate: ParamRow
            {
                key: modelData[0]
                label: modelData[1]
                from: modelData[2]
                to: modelData[3]
                stepSize: modelData[4]
            }
        }

        Repeater
        {
            model: buildingSettings.fileRows
            delegate: FileRow
            {
                key: modelData[0]
                label: modelData[1]
            }
        }

        Button
        {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: buildingSettings.showSaveButton
            text: "Save to index.json"
            onClicked: if (buildingSettings.asset) core.assetsLibrary.save(buildingSettings.asset)
        }

        Item { width: 1; height: 5 }
    }
}
