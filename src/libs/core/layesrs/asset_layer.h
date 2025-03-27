#pragma once
#include "layer.h"

class AssetLayer: public Layer
{
    Q_OBJECT
public:
    explicit AssetLayer(const QString& layerName, QObject* parent = nullptr);
};
