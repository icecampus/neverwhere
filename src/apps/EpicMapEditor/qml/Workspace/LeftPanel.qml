import QtQuick
import QtQuick.Layouts
import QtQuick.Controls 2.15
import Game 1.0
import "../Common"

Rectangle 
{
    property var assetsContext: null
    property int currenPackIndex: 0

    id: leftPanel
    color: colorPalette.surface      

    RowLayout 
    {
        anchors.fill: parent
        Rectangle
        {
            id: tabPanel
            Layout.fillHeight: true
            Layout.minimumWidth: 50
            Layout.maximumWidth: 50
            color: colorPalette.surface2
            
            Column 
            {
                anchors.fill: parent
                Repeater 
                {
                    model: core.assetsLibrary
                    VerticalTab
                    {
                        width: 50
                        height: 50

                        selected: assetsContext.assetPack == element
                                
                        hoveredColor: colorPalette.darkOrange
                        borderColor: colorPalette.surface
                        imageSource: element.thumbnailUrl
                        rounded: false
                        margin: 0
                        onClicked:
                        {
                            currenPackIndex = index
                            assetsContext.assetPack = element
                        }
                    }
                }
            }
        }

        Item
        {
            Layout.fillWidth: true
            Layout.fillHeight: true

            AssetsLibrary
	        {
                id: packLayout
                anchors.fill: parent                    
                currentIndex: currenPackIndex
                
                assetsContext: leftPanel.assetsContext
                model: core.assetsLibrary
            }
            
            FilterInput
            {
                anchors.bottom: parent.bottom
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: 10
            }
        }
    }
}