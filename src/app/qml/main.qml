import QtQuick
import QtQuick.Window
import QtQuick.Shapes 1.15

Window {
    width: 800
    height: 600
    visible: true
    title: "Isometric Map with Camera Control"

    property int tileWidth: 128
    property int tileHeight: 64
    property int cameraX: 400  // Начальное смещение камеры
    property int cameraY: 100  // для центрирования карты

    Component 
    {
        id: tileComponent
        
        Item 
        {
            x: (gridX - gridY) * tileWidth / 2
            y: (gridX + gridY) * tileHeight / 2

            width: tileWidth  // Общая ширина ромба
            height: tileHeight // Высота в 2 раза меньше ширины

            Shape 
            {
                anchors.fill: parent
                ShapePath 
                {
                    fillColor: "lightblue"
                    strokeColor: "darkblue"
                    strokeWidth: 2
            
                    // Вершины ромба с соотношением 2:1
                    PathPolyline 
                    {
                        path: [
                            Qt.point(width/2, 0),          // Верхняя точка
                            Qt.point(width, height/2),     // Правая
                            Qt.point(width/2, height),     // Низ
                            Qt.point(0, height/2),         // Левая
                            Qt.point(width/2, 0)           // Замыкаем путь
                        ]
                    }
                }
            }
        }
    }

    Item {
        anchors.fill: parent
        clip: true  // Обрезаем тайлы за пределами окна

        // Контейнер карты с трансформацией камеры
        Item {
            transform: Translate { x: -cameraX; y: -cameraY }
            
            Repeater {
                model: [
                    {x:0,y:0}, {x:1,y:0}, {x:2,y:0}, {x:3,y:0}, {x:4,y:0},
                    {x:0,y:1}, {x:1,y:1}, {x:2,y:1}, {x:3,y:1}, {x:4,y:1},
                    {x:0,y:2}, {x:1,y:2}, {x:2,y:2}, {x:3,y:2}, {x:4,y:2},
                    {x:0,y:3}, {x:1,y:3}, {x:2,y:3}, {x:3,y:3}, {x:4,y:3},
                    {x:0,y:4}, {x:1,y:4}, {x:2,y:4}, {x:3,y:4}, {x:4,y:4},
                ]

                delegate: Loader {
                    sourceComponent: tileComponent
                    property int gridX: modelData.x
                    property int gridY: modelData.y
                    z: gridX + gridY  // Порядок отрисовки
                }
            }
        }

        // Область управления камерой
        MouseArea {
            anchors.fill: parent
            property int startX: 0
            property int startY: 0
            property int startCamX: 0
            property int startCamY: 0

            onPressed: {
                startX = mouseX
                startY = mouseY
                startCamX = cameraX
                startCamY = cameraY
            }

            onPositionChanged: {
                if (pressed) {
                    // Вычисляем смещение и обновляем позицию камеры
                    var dx = mouseX - startX
                    var dy = mouseY - startY
                    cameraX = startCamX - dx
                    cameraY = startCamY - dy
                }
            }
        }
    }
}