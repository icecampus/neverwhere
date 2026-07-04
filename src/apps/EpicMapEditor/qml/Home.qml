
import QtQuick
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects
import "Common"

Rectangle
{
    signal openTabRequest(string chapterName, var chapter)

    color: colorPalette.background
    property int selectedIndex: -1
    property real baseSize: 80
    property real spacing: 10

    // Inline "create chapter" dialog state.
    property bool createDialogVisible: false

    Item
    {
        anchors
        {
            fill: parent

            topMargin: 5
            bottomMargin: 10
            leftMargin: 20
            rightMargin: 20
        }

        Item
        {
            id: header
            anchors
            {
                top: parent.top
                left: parent.left
                right: parent.right
            }

            height: 40
            Text
            {
                anchors.verticalCenter: parent.verticalCenter
                text: "Chapters"
                font.pixelSize: 20
                color: colorPalette.textPrimary
            }

            // "New chapter" button — opens the inline name dialog.
            Rectangle
            {
                id: newChapterButton
                anchors.verticalCenter: parent.verticalCenter
                anchors.right: parent.right
                width: 32
                height: 32
                radius: 6
                color: newChapterMouseArea.containsMouse ? colorPalette.primaryOrange : colorPalette.surface
                border.color: colorPalette.border

                Text
                {
                    anchors.centerIn: parent
                    text: "+"
                    font.pixelSize: 22
                    color: colorPalette.textPrimary
                }

                MouseArea
                {
                    id: newChapterMouseArea
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked:
                    {
                        chapterNameInput.text = ""
                        createDialogVisible = true
                        chapterNameInput.forceActiveFocus()
                    }
                }
            }
        }

        // Inline dialog for entering the new chapter's name.
        Rectangle
        {
            id: createDialog
            visible: createDialogVisible
            anchors.centerIn: parent
            width: 360
            height: 140
            radius: 12
            color: colorPalette.surface
            border.color: colorPalette.border
            z: 100

            Column
            {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 16

                Text
                {
                    text: "New chapter"
                    color: colorPalette.textPrimary
                    font.pixelSize: 16
                }

                PropertyInput
                {
                    id: chapterNameInput
                    width: parent.width
                    height: 36
                    placeholderText: "Chapter name"
                    Keys.onEnterPressed: createChapterAndClose()
                    Keys.onReturnPressed: createChapterAndClose()
                }

                Row
                {
                    spacing: 10
                    layoutDirection: Qt.RightToLeft

                    Rectangle
                    {
                        width: 90
                        height: 32
                        radius: 6
                        color: createButtonMouseArea.containsMouse ? colorPalette.primaryOrange : colorPalette.surface2
                        border.color: colorPalette.border

                        Text { anchors.centerIn: parent; text: "Create"; color: colorPalette.textPrimary }
                        MouseArea
                        {
                            id: createButtonMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: createChapterAndClose()
                        }
                    }

                    Rectangle
                    {
                        width: 90
                        height: 32
                        radius: 6
                        color: cancelButtonMouseArea.containsMouse ? colorPalette.surface2 : colorPalette.surface
                        border.color: colorPalette.border

                        Text { anchors.centerIn: parent; text: "Cancel"; color: colorPalette.textPrimary }
                        MouseArea
                        {
                            id: cancelButtonMouseArea
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: createDialogVisible = false
                        }
                    }
                }
            }
        }

        GridView
        {
            id: chaptersView

            anchors
            {
                top: header.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }

            property int minCellWidth: 300
            cellWidth: width / Math.max(1, Math.floor(width / minCellWidth))
            cellHeight: cellWidth / 16.0 * 9
            property int selectedIndex: -1

            model: core.chapters
            clip: true
            delegate: Item
            {

                property bool selected: index === chaptersView.selectedIndex
                width:  chaptersView.cellWidth
                height: chaptersView.cellHeight

                Rectangle
                {
                    anchors.fill: parent
                    anchors.margins: 2
                    radius: 12
                    color: colorPalette.surface
                    border.color: colorPalette.border

                    Image
                    {
                        anchors.centerIn: parent
                        width:  chaptersView.cellWidth - 20
                        height: chaptersView.cellHeight - 20

                        source: "image://chaptersImage/" + element.name
                    }

                    Rectangle
                    {
                        id: textBackground
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin: 15
                        width: textItem.width + 20
                        height: textItem.height + 10
                        color: "#80000000" // Полупрозрачный черный (50% непрозрачности)
                        radius: 4

                        Text
                        {
                            id: textItem
                            anchors.centerIn: parent
                            text: element.name
                            color: colorPalette.textPrimary
                            font.pixelSize: 18
                        }
                    }
                }

                MouseArea
                {
                    anchors.fill: parent
                    hoverEnabled: true
                    onEntered: parent.children[0].color = colorPalette.primaryOrange
                    onExited: parent.children[0].color =  colorPalette.surface
                    onClicked:
                    {
                        openTabRequest(element.name, element)
                    }
                }
            }
        }
    }

    function createChapterAndClose()
    {
        var name = chapterNameInput.text.trim()
        if (name.length === 0)
        {
            return
        }

        var chapter = core.chapters.createChapter(name)
        createDialogVisible = false
        if (chapter)
        {
            // Reuse the existing open-flow so MainWindow opens a Workspace tab.
            openTabRequest(chapter.name, chapter)
        }
    }
}

