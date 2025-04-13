#pragma once
#include "game_object.h"

class Landscape : public GameObject
{
    Q_OBJECT

    Q_PROPERTY(size_t tileIndex READ getTileIndex NOTIFY tileIndexChanged )

public:
    explicit Landscape(QObject* parent = nullptr);

    //properties
    size_t getTileIndex() const;
    void setTileIndex(size_t index);

signals:
    void tileIndexChanged();

private:
    size_t tileIndex{0};
};
