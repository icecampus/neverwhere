#pragma once
#include "game_object.h"

class Tile2D : public GameObject
{
    Q_OBJECT

public:
    explicit Tile2D(QObject* parent = nullptr);

    virtual void load(const BaseData::GameObject& data) override;
signals:

private:
};
