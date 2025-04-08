import QtQuick
import QtQuick.Layouts
import QtQuick.Controls 2.15
import Game 1.0
import "../../Common"

StackLayout
{
    property alias model: contentRepeater.model
    id: packLayout

    Repeater
	{
        id: contentRepeater
        AssetsPack
        {
            assetsPackModel: element
        }
    }
           
}
