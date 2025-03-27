#pragma once
#include "layer.h"

class LandLayer: public Layer
{
    Q_OBJECT

public:
    explicit LandLayer(QObject* parent = nullptr);
};
