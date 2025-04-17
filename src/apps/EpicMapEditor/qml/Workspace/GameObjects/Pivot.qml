import QtQuick
import Game 1.0

Canvas 
{
    id: canvas
    
    // Цвет линий
    property color strokeColor: "white"
    // Толщина линий
    property real strokeWidth: 3
    
    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        
        // Настройка стиля рисования
        ctx.strokeStyle = strokeColor
        ctx.lineWidth = strokeWidth
        ctx.save()
        
        // Перенос системы координат в центр
        ctx.translate(width / 2, height / 2)
        
        // Рисуем ромб
        ctx.beginPath()
        ctx.moveTo(width / 2, 0)          // Правая вершина
        ctx.lineTo(0, height / 2)          // Нижняя вершина
        ctx.lineTo(-width / 2, 0)          // Левая вершина
        ctx.lineTo(0, -height / 2)         // Верхняя вершина
        ctx.closePath()
        ctx.stroke()
        
        // Рисуем диагонали
        ctx.beginPath()
        // Горизонтальная диагональ
        ctx.moveTo(-width / 2, 0)
        ctx.lineTo(width / 2, 0)
        // Вертикальная диагональ
        ctx.moveTo(0, -height / 2)
        ctx.lineTo(0, height / 2)
        ctx.stroke()
        
        ctx.restore()
    }
    
    // Автоматически перерисовываем при изменении свойств
    onStrokeColorChanged: requestPaint()
    onStrokeWidthChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
}