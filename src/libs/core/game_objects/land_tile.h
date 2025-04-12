#pragma once
#include "game_object.h"

class LandTile : public GameObject
{
    Q_OBJECT
    Q_PROPERTY(bool isPassable READ isPassable WRITE setPassable NOTIFY passableChanged)

public:
    explicit LandTile(QObject* parent = nullptr);

    bool isPassable() const;
    void setPassable(bool passable);

signals:
    void passableChanged();

private:
    bool m_isPassable;
};
