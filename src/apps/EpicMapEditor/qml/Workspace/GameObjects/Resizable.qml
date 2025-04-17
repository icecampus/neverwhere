import QtQuick
import Game 1.0

Rectangle 
{
    signal updateScreenDragDelta(point screenDelta)
    signal updateScreenWidthDelta(real screenWidthDelta)
    signal startDrag()
    signal endDrag()

    property int borderWidth: 10

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
        width: borderWidth
        color: "red"
    }

    MouseArea 
    {
        property point clickPos: Qt.point(0, 0)
        property int edge: 0

        readonly property int edgeSize: borderWidth
        readonly property int rightEdge: 0x02
        
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
                    updateScreenDragDelta(dragDelta) 
                    return
                }
            
                if (edge & rightEdge) 
                {
                    updateScreenWidthDelta(-dragDelta.x)
                }
            }
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