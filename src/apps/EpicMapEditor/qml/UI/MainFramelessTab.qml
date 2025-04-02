import QtQuick 
import QtQuick.Controls

TabButton
{
	id: control
	property bool closable: false
	signal pinnedClicked()
	signal closeClicked()

	font.pixelSize: 16

    background: Rectangle
    {
		//height: control.height
		color: control.down
			   ? (control.checked ? colorPalette.neonOrange : colorPalette.background)
               : (control.checked ? colorPalette.lightOrange : colorPalette.background )
    }


    contentItem: Text 
	{
        text: control.text
        font: control.font
        color: (control.checked)? colorPalette.textPrimary: "grey"
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
			//iconSource:"qrc:/icons/images/icon-pinned-16-x-16.svg"
		}
		
		MainFramelessButton
		{
			id: maxButton
			height: parent.height

			onClicked:
			{
				closeClicked();
			}
			iconSource: "qrc:/resources/icons/close.png"
		}
	}
}
