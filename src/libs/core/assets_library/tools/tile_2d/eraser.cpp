#include "eraser.h"

Eraser::Eraser(QObject* parent) :
    Tool("Eraser", "eraser", parent)
{

}

void Eraser::click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, DiamondIsometryView* iso,
    bool ctrlModifier, bool shiftModifier, bool altModifier)
{

}
