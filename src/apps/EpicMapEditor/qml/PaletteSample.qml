
import QtQuick
import QtQuick.Controls 2.15
import Qt5Compat.GraphicalEffects 

Rectangle 
{
    ColorPalette
    {
        id: vibrantDarkPalette
    }

    color: vibrantDarkPalette.background
    
    
    Rectangle 
    {
         width: parent.width
         height: 80
         gradient: Gradient {
             GradientStop { position: 0.0; color: vibrantDarkPalette.surface }
             GradientStop { position: 1.0; color: vibrantDarkPalette.surface2 }
         }

         Text {
             text: "VIBRANT UI"
             color: vibrantDarkPalette.primaryOrange
             font {
                 pixelSize: 28
                 bold: true
                 letterSpacing: 2
             }
             anchors.centerIn: parent
         }
    }

    Rectangle 
    {
        width: 160
        height: 50
        radius: 8
        color: vibrantDarkPalette.primaryOrange
        anchors.centerIn: parent
 
        layer.enabled: true
        layer.effect: Glow {
            color: "#80FF9100"
            radius: 16
            samples: 25
        }
 
        Text 
        {
            text: "SUBMIT"
            color: vibrantDarkPalette.background
            font 
            {
                pixelSize: 16
                bold: true
            }
            anchors.centerIn: parent
        }
 
        MouseArea 
        {
            anchors.fill: parent
            hoverEnabled: true
            onEntered: parent.color = vibrantDarkPalette.lightOrange
            onExited: parent.color = vibrantDarkPalette.primaryOrange
        }
    }

    // Карточка с акцентами
    Rectangle 
    {
        width: 300
        height: 180
        radius: 12
        color: vibrantDarkPalette.surface
        anchors {
            bottom: parent.bottom
            horizontalCenter: parent.horizontalCenter
            margins: 30
        }

        border 
        {
            width: 1
            color: vibrantDarkPalette.primaryOrange
        }

        Column 
        {
            anchors 
            {
                top: parent.top
                left: parent.left
                margins: 20
                right: parent.right
            }
            spacing: 12

            Text 
            {
                text: "🔥 Active Items"
                color: vibrantDarkPalette.textPrimary
                font.pixelSize: 18
            }

            Rectangle 
            {
                width: parent.width - 40
                height: 4
                radius: 2
                color: vibrantDarkPalette.surface2
             
                Rectangle 
                {
                    width: parent.width * 0.75
                    height: parent.height
                    radius: 2
                    color: vibrantDarkPalette.primaryOrange
                }
            }

            Text 
            {
                text: "● 3 new notifications"
                color: vibrantDarkPalette.lightOrange
                font.pixelSize: 14
            }
        }
    }
    
}

