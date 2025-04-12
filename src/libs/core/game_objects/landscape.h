#pragma once
#include "game_object.h"

class Landscape : public GameObject
{
    Q_OBJECT

public:
    explicit Landscape(QObject* parent = nullptr);

signals:

private:
};
