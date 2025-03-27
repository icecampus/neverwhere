import QtQuick
import Game 1.0

Item 
{
    property GameObject gameObject: null
    property real radius: isoView.dimensions.cellSize.x / 4

    // Позиция относительно контейнера, без учета камерыc
    x: isoView.mapToScreen(gameObject.position).x - radius
    y: isoView.mapToScreen(gameObject.position).y - radius
    width: isoView.dimensions.cellSize.x * 2
    height: isoView.dimensions.cellSize.y * 4
    z: isoView.zOffset(gameObject.position)


    property var imageSources: [
        "image://assetImages/a3771d55-8ca0-44aa-9d9f-5ab3e9cb300e", // Оригинальное изображение
        "image://assetImages/9813e80b-c6f7-43f9-9f11-f074009bb8f1", // Вариант 1
        "image://assetImages/737179d4-0535-4141-ab2c-68758b71c141", // Вариант 2
        "image://assetImages/a32aee74-1e74-45c4-a34c-de5e8847d1da", // Вариант 3
        "image://assetImages/a476f78f-c329-4a78-be82-b7c5dbe49c6c"  // Вариант 4
    ]

    property int randomIndex: Math.floor(Math.random() * 5)

    Image 
    {
        anchors.fill: parent
        source: imageSources[randomIndex]
    }
}