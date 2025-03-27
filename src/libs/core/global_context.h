#pragma once
#include <QObject>
#include "layesrs/layer.h"

class GlobalContext: public QObject
{
    Q_OBJECT
    Q_PROPERTY(LayersLibrary* layersLibraty READ getLayersLibraty CONSTANT);
public:
    explicit GlobalContext(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    LayersLibrary* getLayersLibraty();

private:
    LayersLibrary layerLibrary;
};
