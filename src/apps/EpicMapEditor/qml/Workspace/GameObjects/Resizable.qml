import QtQuick
import Game 1.0

Rectangle 
{
    signal updateScreenDragDelta(point screenDelta)
    signal updateScreenWidthDelta(real screenWidthDelta)
    signal startDrag()
    signal endDrag()

    property int borderWidth: 15
    property int edge: 0
    readonly property int rightEdge: 0x02

    id: root

    color: "gray"
    opacity: 0.5
    border.color: "black"
    border.width: 1

    Rectangle
    {
        id: activeEdge
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        anchors.right: parent.right
        width: 5
        color: (resizeHandler.containsMouse && (edge & rightEdge))? "black" : "transparent"
    }

    MouseArea 
    {
        property point clickPos: Qt.point(0, 0)
        

        readonly property int edgeSize: borderWidth
        
        
        id: resizeHandler
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton
        hoverEnabled: true
        cursorShape: containsMouse ? Qt.SizeAllCursor : Qt.ArrowCursor
        
        // Определяем границы для изменения размера
        function getEdge(mousePos) 
        {
            var edge = 0
            if (mousePos.x > (width - edgeSize))
            { 
                edge = rightEdge
            }
            return edge
        }
        
        onPressed:(mouse)=>  
        {
            clickPos = Qt.point(mouse.x, mouse.y)
            edge = getEdge(clickPos)
            startDrag()
        }
        
        onReleased:(mouse)=>
        {
            endDrag()
        }

        onPositionChanged:(mouse)=> 
        {
            if (pressed) 
            {
                var dragDelta = Qt.point(clickPos.x - mouse.x  , clickPos.y - mouse.y)

                if (pressed && !edge) 
                {
                    // !!!!!!!!!!!!!!!!!!!!!!!!!
                    // когда двигается весь элемент - он начинает следовать за мышкой и и dragDelta содержит сдвиг с прошолого кадра
                    updateScreenDragDelta(dragDelta) 
                    return
                }
            
                if (edge & rightEdge) 
                {
                    // !!!!!!!!!!!!!!!!!!!!!!!!!
                    // когда двигается грань - dragDelta содержит сдвиг относительно точки нажатия, 
                    // то есть обсалютный сдвиг относительно начала перетаскивания
                    updateScreenWidthDelta(-dragDelta.x)
                }
            }

            edge = getEdge(Qt.point(mouse.x, mouse.y))
        }
        
        onExited:
        {
            //edge = 0
        }
        
        onCursorShapeChanged: 
        {
            if (edge == 0) 
            {
                cursorShape = Qt.ArrowCursor
                return
            }
            /*            
            if (edge &  rightEdge) 
            {
                cursorShape = Qt.SizeHorCursor
            }
            */
        }
    }
    //*/
}