import QtQuick
import Game 1.0


Item 
{
    property alias imageSource: image.source

    property real pivotX
    property real pivotY
    property bool active: false

    function updatePivot(pivot)
    {
        active = false
        console.log("pivot.x: " + pivot.x + ", pivot.y: " + pivot.y)
        circle.x = (container.width - circle.width) * pivot.x
        circle.y = (container.height - circle.height) * pivot.y
        active = true
    }

    id: container

    Image
    {
        id: image
        anchors.fill: parent
    }

    Rectangle 
    {
        id: circle
        width: 20
        height: 20
        radius: width / 2
        color: "red"
        border.color: "darkred"
        border.width: 2
        
        x: (parent.width - width) / 2  // Начальная позиция по центру
        y: (parent.height - height) / 2

        onXChanged:
        {   
            if(active)
            {
                pivotX = circle.x / container.width
            }
        }

        onYChanged:
        {
            if(active)
            {
                pivotY = circle.y / container.height
            }
        }

        MouseArea 
        {
            id: dragArea
            anchors.fill: parent
            drag 
            {
                target: circle
                // Ограничиваем перемещение в пределах родительского контейнера
                axis: Drag.XAndYAxis
                minimumX: 0
                maximumX: container.width - circle.width
                minimumY: 0
                maximumY: container.height - circle.height
            }
            
            // Анимация при клике
            onPressed: {
                circle.scale = 0.9
                circle.color = "#fcb1b1"
            }
            
            onReleased: 
            {
                circle.scale = 1.0
                circle.color = "red"
            }
        }
       
        // Анимация масштаба
        Behavior on scale { NumberAnimation { duration: 100 } }
    }
}