#include "land_layer.h"

namespace{
    const QString layerName = "Landscape";
}

LandLayer::LandLayer( QObject* parent):
    Layer(layerName, parent)
{

}

