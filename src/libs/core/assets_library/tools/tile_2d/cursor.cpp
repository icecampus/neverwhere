#include "cursor.h"

Cursor::Cursor(QObject* parent):
    Tool("Cursor", "cursor", parent)
{

}

void Cursor::click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, StaggeredIsometryView* iso, bool ctrlModifier, bool shiftModifier, bool altModifier)
{

}

