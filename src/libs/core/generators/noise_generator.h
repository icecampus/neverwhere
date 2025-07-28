#pragma once
#include <QObject>

#include "topology/staggered_tiled_landscape.h"
#include "map/map_model.h"
#include "assets_library/asset.h"

struct PerlinGen
{
    using GeneratorLayerData = std::vector<int>;
    static LandNodes generate(float frequency, float secondOctave, float thirdOctave, float waterLevel);
};

//
class NoiseGenerator: public QObject
{
    Q_OBJECT
    Q_PROPERTY(Asset* currentAsset READ getCurrentAsset WRITE setCurrentAsset NOTIFY currentAssetChanged)
public:

    //properties
    Asset* getCurrentAsset();
    void setCurrentAsset(Asset* asset);

    //
    Q_INVOKABLE void generate(MapModel* mapModel);

signals:
    void currentAssetChanged();

private:
    void generate(MapModel* mapModel,  SliceAsset* sliceAsset);

    Asset* currentAsset{nullptr};
};