import QtQuick 2.7
import QtQuick.Controls 2.3

TabButton
{
	id: control
	property bool closable: false
	signal pinnedClicked()
	signal closeClicked()

    background: Rectangle
    {
		color: control.down
			   ? (control.checked ? "#000000" : "transparent")
               : (control.checked ? "#000000" : "transparent")
    }


    contentItem: Text {
        text: control.text
        font: control.font
        color: (control.checked)?"white": "grey"
		elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
    }

	//pinbutton
	Row
	{
		anchors.top: parent.top
		anchors.right:  parent.right
		anchors.bottom: parent.bottom

		spacing: 2
		visible: closable

		MainFramelessButton
		{
			id: minButton
			height: parent.height
			onClicked:
			{
				pinnedClicked();
			}
			// iconSource:"qrc:/icons/images/icon-pinned-16-x-16.svg"
		}
		
		MainFramelessButton
		{
			id: maxButton
			height: parent.height

			onClicked:
			{
				closeClicked();
			}
			// iconSource: "qrc:/icons/images/window-close.png"
		}
	}
}
