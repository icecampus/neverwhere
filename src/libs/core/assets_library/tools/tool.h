#pragma once
#include <QObject>
#include "simple_model.h"
#include "assets_library/asset.h"
#include "map/map_model.h"
#include "topology/diamond_isometry.h"

//Tool
enum class StrokeKind
{
    Begin = 0,
    Move = 1,
    End = 2
};

class Tool : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString icon READ icon CONSTANT)
public:
    explicit Tool(const QString& name, const QString& icon, QObject* parent);

    QString name() const;
    QString icon() const;


    virtual void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, DiamondIsometryView* iso,
        bool ctrlModifier, bool shiftModifier, bool altModifier)=0;

    // Drag-stroke entry point (mouse press -> moves -> release). Default:
    // Begin behaves as a single click, Move/End are ignored — tools that
    // support continuous painting (LandscapePencil) override this.
    virtual void stroke(StrokeKind kind, QPoint screenPos, Asset* currentAsset, LayerModel* mapModel,
        DiamondIsometryView* iso, bool ctrlModifier, bool shiftModifier, bool altModifier);
private:
    QString _name;
    QString _icon;

};

