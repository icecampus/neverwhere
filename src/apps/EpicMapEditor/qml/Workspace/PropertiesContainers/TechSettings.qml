import QtQuick
import Game 1.0
import QtQuick.Controls 2.15

// Settings panel for tech3d assets: the tech-field generator + shading
// parameter set (TechnicalGrass ridge/valley heightfield), edited live (the
// renderer applies edits on the next frame sync; field edits rebuild through
// the debounce). Save persists the payload to the asset's index.json.
// Hosted inside the AssetSettings right-panel block (asset in editMode) with
// its own title/save hidden; can also stand alone (showTitle/showSaveButton).
Rectangle
{
    property var asset: null
    property alias text: titleText.text
    // Set by the host: the panel content only shows for tech3d assets.
    property bool activeAsset: false
    // Standalone-hosting knobs (off when embedded into AssetSettings).
    property bool showTitle: true
    property bool showSaveButton: true

    id: techSettings
    color: colorPalette.surface2
    border.color: colorPalette.border
    radius: 5

    visible: activeAsset
    height: activeAsset ? (title.height + content.height + 10) : 0

    // Re-read when the selected asset changes; rows also read through `params`.
    // Self-contained guard: plain assets have no techParams() invokable, and
    // going through the `activeAsset` property here would race (QML does not
    // order independent binding updates — params can re-evaluate with a stale
    // activeAsset right after the asset switches tech -> non-tech).
    property var params: (asset && asset.isTech3d) ? asset.techParams() : ({})

    // [key, label, from, to, stepSize] — ranges mirror the SDFGeneratedLandscape
    // ImGui panel.
    property var sections: [
        {
            "title": "Field",
            "rows": [
                ["raisedHeight", "Raised height", 4.0, 128.0, 1.0],
                ["levelHeight", "Level height", 0.1, 1.0, 0.01],
                ["groundDepth", "Ground depth", 0.02, 0.3, 0.01],
                ["cellSize", "Cell size", 0.04, 0.12, 0.001]
            ],
            "checks": []
        },
        {
            "title": "Style",
            "rows": [
                ["style", "Style (ridge-valley)", 0.0, 1.0, 0.01],
                ["soften", "Soften ramps", 0.0, 1.0, 0.01],
                ["creaseWidth", "Crease width", 0.0, 0.2, 0.005],
                ["outlineDepth", "Outline depth", 0.0, 2.0, 0.01]
            ],
            "checks": []
        },
        {
            "title": "Shading",
            "rows": [
                ["shading.lightAzimuth", "Light azimuth", -3.14, 3.14, 0.01],
                ["shading.lightElevation", "Light elevation", 0.0, 1.57, 0.01],
                ["shading.veinThreshold", "Vein threshold", 0.6, 2.5, 0.01],
                ["shading.ambient", "Ambient", 0.05, 0.8, 0.01],
                ["shading.diffuse", "Diffuse", 0.0, 1.2, 0.01],
                ["shading.backLight", "Back light", 0.0, 0.4, 0.01],
                ["shading.specStrength", "Spec strength", 0.0, 1.5, 0.01],
                ["shading.specPower", "Spec power", 4.0, 64.0, 1.0],
                ["shading.gamma", "Gamma", 0.5, 1.5, 0.01]
            ],
            "checks": []
        },
        {
            "title": "Colors",
            "collapsed": true,
            "rows": [
                ["shading.darkColor.0", "Dark R", 0.0, 1.0, 0.01],
                ["shading.darkColor.1", "Dark G", 0.0, 1.0, 0.01],
                ["shading.darkColor.2", "Dark B", 0.0, 1.0, 0.01],
                ["shading.goldColor.0", "Gold R", 0.0, 1.0, 0.01],
                ["shading.goldColor.1", "Gold G", 0.0, 1.0, 0.01],
                ["shading.goldColor.2", "Gold B", 0.0, 1.0, 0.01],
                ["shading.grassA.0", "Grass A R", 0.0, 1.0, 0.01],
                ["shading.grassA.1", "Grass A G", 0.0, 1.0, 0.01],
                ["shading.grassA.2", "Grass A B", 0.0, 1.0, 0.01],
                ["shading.grassB.0", "Grass B R", 0.0, 1.0, 0.01],
                ["shading.grassB.1", "Grass B G", 0.0, 1.0, 0.01],
                ["shading.grassB.2", "Grass B B", 0.0, 1.0, 0.01]
            ],
            "checks": []
        },
        {
            "title": "Advanced",
            "collapsed": true,
            "rows": [
                ["padding", "Field padding", 0.1, 1.0, 0.05],
                ["blurPasses", "Blur passes", 0.0, 3.0, 1.0],
                ["shading.bottomDarken", "Bottom darken", 0.0, 1.0, 0.01],
                ["shading.bottomBand", "Bottom band", 0.02, 1.0, 0.01],
                ["shading.strataStrength", "Strata bands", 0.0, 0.5, 0.01]
            ],
            "checks": []
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
            value: techSettings.params[key] !== undefined ? techSettings.params[key] : 0
            anchors.verticalCenter: parent.verticalCenter
            onMoved: if (techSettings.asset) techSettings.asset.setTechParam(key, value)
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
        }
    }

    Rectangle
    {
        id: title
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        visible: techSettings.showTitle
        height: techSettings.showTitle ? 30 : 0
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
            model: techSettings.sections
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
            visible: techSettings.showSaveButton
            text: "Save to index.json"
            onClicked: if (techSettings.asset) core.assetsLibrary.save(techSettings.asset)
        }

        Item { width: 1; height: 5 }
    }
}
