import QtQuick 2.15
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 

TextField 
{
    id: filter
    placeholderText: (activeFocus)? "" : "Filter..."
    placeholderTextColor: colorPalette.textSecondary
    color: colorPalette.textPrimary
    rightPadding: clearButton.visible ? clearButton.width + 5 : 0 // Отступ справа, когда кнопка видна

    background: Item 
    {
        // Фон поля ввода
        Rectangle 
        {
            id: bg
            anchors.fill: parent
            color: colorPalette.surface
            border.color: parent.parent.focus ? colorPalette.primaryOrange : colorPalette.border
            border.width: 1
            radius: 4
        }
    }

    Keys.onPressed: (event) => {
        if (event.key === Qt.Key_Escape) 
        {
            filter.text = ""
            filter.focus = false
            event.accepted = true
        }
    }

    // Кнопка стирания
    Rectangle 
    {
        id: clearButton
        width: 32
        height: 32
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        color: "transparent" // Прозрачный фон кнопки
        visible: filter.length > 0 // Кнопка видна только при наличии текста

        // Символ "×" для кнопки
        Image 
        {
            anchors.centerIn: parent
            source: "qrc:/resources/icons/clear.png"
            width: 32
            height: 32
        }

        // Обработка нажатия
        MouseArea 
        {
            anchors.fill: parent
            onClicked: filter.text = "" // Очистка текста
        }
    }
}