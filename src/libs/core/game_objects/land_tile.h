#pragma once
#include "game_object.h"

class LandTile : public GameObject
{
    Q_OBJECT
        Q_PROPERTY(bool isPassable READ isPassable WRITE setPassable NOTIFY passableChanged)

public:
    explicit LandTile(QObject* parent = nullptr) : GameObject(parent), m_isPassable(true) {}

    bool isPassable() const {
        return m_isPassable;
    }

    void setPassable(bool passable) {
        if (m_isPassable != passable) {
            m_isPassable = passable;
            emit passableChanged();
        }
    }

signals:
    void passableChanged();

private:
    bool m_isPassable;
};
