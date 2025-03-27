#pragma once
#include <QObject>
#include "layesrs/layer.h"
#include "models/chapters_model.h"

class GlobalContext: public QObject
{
    Q_OBJECT
    Q_PROPERTY(LayersLibrary* layersLibraty READ getLayersLibraty CONSTANT);
    Q_PROPERTY(ChaptersModel* chapters READ getChaptersModel CONSTANT);
public:
    explicit GlobalContext(QObject* parent = nullptr);

    Q_INVOKABLE void load();

    LayersLibrary* getLayersLibraty();
    ChaptersModel* getChaptersModel();

private:
    LayersLibrary layerLibrary;
    ChaptersModel chaptersModel;
};
