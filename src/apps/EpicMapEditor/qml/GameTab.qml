import QtQuick
import QtQuick.Controls 2.15
import Game 1.0

// Play-test tab: an isolated game runtime (RuntimeFrameSource) rendering its
// world through the same MapRenderItem/WorldRenderer as the editor workspace
// and the standalone client. One tab per chapter (dedup by tab name); a
// repeated Play restarts the session via applyExtraData().
Item
{
    id: gameTab

    property var chapter: null

    // Called by MainWindow's delegate right after the tab item is created.
    function load(data, extraData)
    {
        chapter = core.chapters.getByUuid(data.chapterUuid)
        runtimeSource.start(chapter.mapPath)
        applyCamera(extraData)
    }

    // Called on a repeated Play for an already open tab (fresh extraData):
    // restart the session and re-focus the camera on the editor's position.
    function applyExtraData(extraData)
    {
        runtimeSource.restart()
        applyCamera(extraData)
    }

    function applyCamera(extraData)
    {
        if (extraData === null || extraData === undefined)
            return
        if (extraData.camX !== undefined) isoView.cameraX = extraData.camX
        if (extraData.camY !== undefined) isoView.cameraY = extraData.camY
        if (extraData.camZoom !== undefined) isoView.cameraZoom = extraData.camZoom
    }

    RuntimeFrameSource
    {
        id: runtimeSource
    }

    // Same renderer as the editor workspace / the standalone client.
    MapRenderItem
    {
        id: mapRenderItem
        anchors.fill: parent

        frameSource: runtimeSource

        cameraX: isoView.cameraX
        cameraY: isoView.cameraY
        cameraZoom: isoView.cameraZoom

        cursorCell: Qt.point(mouseArea.hoveredX, mouseArea.hoveredY)
        showGrid: true
    }

    DiamondIsometryView
    {
        id: isoView
    }

    // Camera controls — same behavior as the workspace MapMouseArea
    // (RMB pan, wheel zoom-to-cursor), minus the editing tools.
    MouseArea
    {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.RightButton

        property int hoveredX: 0
        property int hoveredY: 0

        property int startX: 0
        property int startY: 0
        property real startCamX: 0
        property real startCamY: 0
        property bool startDrag: false

        onPressed:(mouse)=>
        {
            if(mouse.button === Qt.RightButton )
            {
                startX = mouseX
                startY = mouseY
                startCamX = isoView.cameraX
                startCamY = isoView.cameraY
                startDrag = true
            }
        }

        onReleased:(mouse)=>
        {
            if(mouse.button === Qt.RightButton )
            {
                startDrag = false
            }
        }

        onPositionChanged:(mouse)=>
        {
            if (startDrag)
            {
                var dx = mouseX - startX
                var dy = mouseY - startY
                isoView.cameraX = startCamX + dx
                isoView.cameraY = startCamY + dy
            }

            var cellPos = isoView.screenToMap(math.vec2(mouseArea.mouseX, mouseArea.mouseY))
            hoveredX = cellPos.x
            hoveredY = cellPos.y
        }

        onWheel: (wheel) =>
        {
            var delta = wheel.angleDelta.y
            if (delta === 0) return

            var zoomFactor = delta > 0 ? 1.1 : 0.9
            var oldZoom = isoView.cameraZoom
            var newZoom = Math.max(0.1, Math.min(3.0, oldZoom * zoomFactor))

            var mapX = (wheel.x - isoView.cameraX) / oldZoom
            var mapY = (wheel.y - isoView.cameraY) / oldZoom

            isoView.cameraX = wheel.x - mapX * newZoom
            isoView.cameraY = wheel.y - mapY * newZoom
            isoView.cameraZoom = newZoom
        }
    }

    // Debug overlay (standalone client shows the same data via ImGui).
    // NB: top-LEFT on purpose — the custom window frame is broken on High-DPI
    // (QML content overflows the OS window to the right), so anything anchored
    // to the right edge can land off-screen in windowed mode.
    Rectangle
    {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: 10
        width: debugText.width + 20
        height: debugText.height + 12
        color: "#B3000000"
        radius: 5

        Text
        {
            id: debugText
            anchors.centerIn: parent
            color: "white"
            font.pixelSize: 12
            text: (runtimeSource.lastError !== ""
                    ? "START FAILED: " + runtimeSource.lastError + "\n" : "")
                + "session: " + runtimeSource.sessionTime.toFixed(1) + " s\n"
                + "world: day " + runtimeSource.worldDay + " "
                + ("0" + runtimeSource.worldHour).slice(-2) + ":"
                + ("0" + runtimeSource.worldMinute).slice(-2) + "\n"
                + "cell: (" + mouseArea.hoveredX + ", " + mouseArea.hoveredY + ")"
        }
    }
}
