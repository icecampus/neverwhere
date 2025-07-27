#pragma once
#include <QObject>

#include "topology/staggered_tiled_landscape.h"
#include "map/map_model.h"

struct PerlinGen
{
    using GeneratorLayerData = std::vector<int>;
    static LandNodes generate(float frequency, float secondOctave, float thirdOctave, float waterLevel);
};

//
class NoiseGenerator: public QObject
{
    Q_OBJECT
public:
    Q_INVOKABLE void generatre(MapModel* mapModel);


};