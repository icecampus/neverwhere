#include "tool.h"

Tool::Tool(const QString& name_, const QString& icon_, QObject* parent /*= nullptr*/):
    QObject(parent),
    _name(name_),
    _icon(icon_)
{

}

QString Tool::name() const
{
    return _name;
}

QString Tool::icon() const
{
    return "qrc:/resources/icons/tools/" + _icon + ".png";
}

void Tool::stroke(StrokeKind kind, QPoint screenPos, Asset* currentAsset, LayerModel* mapModel,
    DiamondIsometryView* iso, bool ctrlModifier, bool shiftModifier, bool altModifier)
{
    if (kind == StrokeKind::Begin)
    {
        click(screenPos, currentAsset, mapModel, iso, ctrlModifier, shiftModifier, altModifier);
    }
}

