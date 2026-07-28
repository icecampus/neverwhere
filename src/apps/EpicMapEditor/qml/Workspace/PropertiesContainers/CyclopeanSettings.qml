import QtQuick
import Game 1.0
import QtQuick.Controls 2.15

// Settings panel for cyclopean3d assets: the landscape_mesh composer params
// (Cyclopean wall style), edited live (the renderer applies edits on the next
// frame sync; mesh edits rebuild through the debounce). Save persists the
// payload to the asset's index.json.
// Hosted inside the AssetSettings right-panel block (asset in editMode) with
// its own title/save hidden; can also stand alone (showTitle/showSaveButton).
Rectangle
{
    property var asset: null
    property alias text: titleText.text
    // Set by the host: the panel content only shows for cyclopean3d assets.
    property bool activeAsset: false
    // Standalone-hosting knobs (off when embedded into AssetSettings).
    property bool showTitle: true
    property bool showSaveButton: true

    id: cyclopeanSettings
    color: colorPalette.surface2
    border.color: colorPalette.border
    radius: 5

    visible: activeAsset
    height: activeAsset ? (title.height + content.height + 10) : 0

    // Re-read when the selected asset changes; rows also read through `params`.
    // Self-contained guard: plain assets have no cyclopeanParams() invokable,
    // and going through the `activeAsset` property here would race (QML does
    // not order independent binding updates).
    property var params: (asset && asset.isCyclopean3d) ? asset.cyclopeanParams() : ({})

    // [key, label, from, to, stepSize] — ranges mirror the TileShapePlayground
    // ImGui panel.
    property var sections: [
        {
            "title": "Height",
            "rows": [
                ["raisedHeight", "Raised height", 0.5, 8.0, 0.1]
            ],
            "checks": []
        },
        {
            "title": "Rock",
            "rows": [
                ["rockSeed", "Rock seed", 0.0, 9999.0, 1.0],
                ["rockAmplitude", "Rock amplitude", 0.0, 1.0, 0.01],
                ["cornerBevel", "Corner bevel", 0.0, 0.45, 0.01],
                ["wallSubdivH", "Wall subdiv H", 4.0, 16.0, 1.0],
                ["wallSubdivV", "Wall subdiv V", 4.0, 16.0, 1.0]
            ],
            "checks": [
                ["rockEnabled", "Rock displacement"]
            ]
        }
    ]

    component SectionRow: Row
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
            value: cyclopeanSettings.params[key] !== undefined ? cyclopeanSettings.params[key] : 0
            anchors.verticalCenter: parent.verticalCenter
            onMoved: if (cyclopeanSettings.asset) cyclopeanSettings.asset.setCyclopeanParam(key, value)
        }
        Text
        {
            width: parent.width * 0.16
            text: Number(slider.value).toFixed(3)
            color: colorPalette.textPrimary
            font.pixelSize: 11
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    component Section: Column
    {
        property string title: ""
        property var rows: []
        property var checks: []
        property bool collapsed: false

        width: parent ? parent.width : 0
        spacing: 2

        Row
        {
            width: parent.width
            height: 24
            spacing: 5
            Text
            {
                text: (parent.parent.collapsed ? "▸ " : "▾ ") + title
                color: colorPalette.textPrimary
                font.pixelSize: 13
                font.bold: true
                anchors.verticalCenter: parent.verticalCenter
            }
            TapHandler
            {
                onTapped: parent.parent.collapsed = !parent.parent.collapsed
            }
        }
        Column
        {
            width: parent.width
            spacing: 2
            visible: !parent.collapsed

            Repeater
            {
                model: parent.parent.rows
                delegate: SectionRow
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
                model: parent.parent.checks
                delegate: Row
                {
                    width: parent ? parent.width : 0
                    height: 26
                    spacing: 5
                    Text
                    {
                        width: parent.width * 0.38
                        text: modelData[1]
                        color: colorPalette.textPrimary
                        font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter
                        elide: Text.ElideRight
                    }
                    CheckBox
                    {
                        checked: cyclopeanSettings.params[modelData[0]] === true
                        anchors.verticalCenter: parent.verticalCenter
                        onToggled: if (cyclopeanSettings.asset) cyclopeanSettings.asset.setCyclopeanParam(modelData[0], checked)
                    }
                }
            }
        }
    }

    Rectangle
    {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: cyclopeanSettings.showTitle
        height: cyclopeanSettings.showTitle ? 30 : 0
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
        spacing: 6

        Repeater
        {
            model: cyclopeanSettings.sections
            delegate: Section
            {
                title: modelData.title
                rows: modelData.rows
                checks: modelData.checks
                collapsed: modelData.collapsed === true
            }
        }

        Button
        {
            anchors.horizontalCenter: parent.horizontalCenter
            visible: cyclopeanSettings.showSaveButton
            text: "Save to index.json"
            onClicked: if (cyclopeanSettings.asset) core.assetsLibrary.save(cyclopeanSettings.asset)
        }

        Item { width: 1; height: 5 }
    }
}
