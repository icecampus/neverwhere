#pragma once
#include <QObject>
#include "simple_model.h"
#include "assets_library/asset.h"
#include "map/map_model.h"
#include "topology/staggered_isometry.h"

//Tool
class Tool : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString icon READ icon CONSTANT)
public:
    explicit Tool(const QString& name, const QString& icon, QObject* parent);

    QString name() const;
    QString icon() const;


    virtual void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, StaggeredIsometryView* iso, 
        bool ctrlModifier, bool shiftModifier, bool altModifier)=0;
private:
    QString _name;
    QString _icon;

};

