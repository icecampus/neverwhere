import QtQuick 2.15
import QtQuick.Controls 2.15

TextField {
    id: customField
    width: 300
    height: 40
    
    // Основные настройки
    placeholderText: "Введите текст..."
    maximumLength: 50
    selectByMouse: true  // разрешаем выделение мышью
    
    // Стиль текста
    color: "#333333"
    font {
        pixelSize: 16
        family: "Arial"
    }
    
    // Стиль плейсхолдера
    placeholderTextColor: "#999999"
    
    // Кастомный фон
    background: Rectangle {
        implicitWidth: 300
        implicitHeight: 40
        radius: 5
        color: customField.enabled ? "#FFFFFF" : "#F5F5F5"
        border {
            width: customField.activeFocus ? 2 : 1
            color: customField.activeFocus ? "#4285F4" : "#CCCCCC"
        }
        
        // Анимация изменения границы
        Behavior on border.color {
            ColorAnimation { duration: 200 }
        }
    }
    
    // Дополнительные элементы
    Item {
        anchors {
            right: parent.right
            rightMargin: 10
            verticalCenter: parent.verticalCenter
        }
        visible: customField.text.length > 0
        
        Image {
            source: "qrc:/resources/icons/clear.png"
            width: 16
            height: 16
            MouseArea {
                anchors.fill: parent
                onClicked: customField.clear()
            }
        }
    }
}