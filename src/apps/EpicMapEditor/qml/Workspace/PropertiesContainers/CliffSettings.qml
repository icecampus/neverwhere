import QtQuick
import Game 1.0
import QtQuick.Controls 2.15

// Settings panel for cliff3d assets: the whole cliff-field generator +
// shading parameter set, edited live (the renderer applies edits on the next
// frame sync; field edits rebuild through the debounce). Save persists the
// payload to the asset's index.json.
Rectangle
{
    property var asset: null
    property alias text: titleText.text
    // Set by the host (RightPanel): the panel exists for every asset but is
    // only shown for cliff3d ones.
    property bool activeAsset: false

    id: cliffSettings
    color: colorPalette.surface2
    border.color: colorPalette.border
    radius: 5

    visible: activeAsset
    height: activeAsset ? (title.height + content.height + 10) : 0

    // Re-read when the selected asset changes; rows also read through `params`.
    property var params: asset ? asset.cliffParams() : ({})

    // [key, label, from, to, stepSize] — ranges mirror the TileShapePlayground
    // ImGui panel.
    property var sections: [
        {
            "title": "Field",
            "rows": [
                ["raisedHeight", "Raised height", 4.0, 128.0, 1.0],
                ["cellSize", "Cell size", 0.03, 0.07, 0.001],
                ["plateauHeight", "Plateau height", 0.4, 2.0, 0.01],
                ["blurPasses", "Blur passes", 0.0, 6.0, 1.0],
                ["edgeRadius", "Edge radius", 0.0, 0.12, 0.005],
                ["fbmAmplitude", "Fbm amplitude", 0.0, 0.08, 0.002],
                ["fbmFrequency", "Fbm frequency", 2.0, 10.0, 0.1],
                ["fbmOctaves", "Fbm octaves", 1.0, 3.0, 1.0]
            ],
            "checks": [
                ["groundEnabled", "Ground slab (underlay)"]
            ]
        },
        {
            "title": "Grooves",
            "rows": [
                ["groovePeriod", "Period", 0.2, 0.8, 0.01],
                ["grooveDepthMax", "Depth", 0.02, 0.2, 0.005],
                ["grooveMaskWidth", "Mask width", 0.05, 0.6, 0.01],
                ["grooveFadeK", "Fade K", 0.2, 2.0, 0.05],
                ["grooveRimFade", "Rim fade", 0.0, 0.4, 0.01],
                ["grooveSmooth", "Smooth radius", 0.005, 0.06, 0.001],
                ["groovePhase", "Phase", 0.0, 0.8, 0.01]
            ],
            "checks": []
        },
        {
            "title": "Shading",
            "rows": [
                ["shading.lightAzimuth", "Light azimuth", -3.14, 3.14, 0.01],
                ["shading.lightElevation", "Light elevation", 0.0, 1.57, 0.01],
                ["shading.veinThreshold", "Vein threshold", 0.6, 0.95, 0.01],
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
                ["d2Scale", "Outline scale (d2)", 0.1, 1.0, 0.05],
                ["blurRadiusCells", "Blur radius cells", 1.0, 6.0, 1.0],
                ["groundDepth", "Ground depth", 0.0, 1.0, 0.05],
                ["groundMargin", "Ground margin", 0.0, 1.0, 0.05],
                ["groundRounding", "Ground rounding", 0.0, 0.3, 0.01],
                ["grooveAngles.0.0", "Groove frame 1 rot XY", -3.14, 3.14, 0.01],
                ["grooveAngles.0.1", "Groove frame 1 rot 2", -3.14, 3.14, 0.01],
                ["grooveAngles.1.0", "Groove frame 2 rot XZ", -3.14, 3.14, 0.01],
                ["grooveAngles.1.1", "Groove frame 2 rot XY", -3.14, 3.14, 0.01],
                ["grooveAngles.2.0", "Groove frame 3 rot XZ", -3.14, 3.14, 0.01],
                ["grooveAngles.2.1", "Groove frame 3 rot XY", -3.14, 3.14, 0.01]
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
            value: cliffSettings.params[key] !== undefined ? cliffSettings.params[key] : 0
            anchors.verticalCenter: parent.verticalCenter
            onMoved: if (cliffSettings.asset) cliffSettings.asset.setCliffParam(key, value)
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
                        checked: cliffSettings.params[modelData[0]] === true
                        anchors.verticalCenter: parent.verticalCenter
                        onToggled: if (cliffSettings.asset) cliffSettings.asset.setCliffParam(modelData[0], checked)
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
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 5
        spacing: 6

        Repeater
        {
            model: cliffSettings.sections
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
            text: "Save to index.json"
            onClicked: if (cliffSettings.asset) core.assetsLibrary.save(cliffSettings.asset)
        }

        Item { width: 1; height: 5 }
    }
}
