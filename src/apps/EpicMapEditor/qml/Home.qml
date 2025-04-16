
import QtQuick
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 

Rectangle 
{
    signal openTabRequest(string chapterName, var chapter)
    
    color: colorPalette.background 
    property int selectedIndex: -1
    property real baseSize: 80
    property real spacing: 10

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
}

