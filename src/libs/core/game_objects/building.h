#pragma once
#include "game_object.h"

class Building : public GameObject
{
    Q_OBJECT
        Q_PROPERTY(int level READ level WRITE setLevel NOTIFY levelChanged)

public:
    explicit Building(QObject* parent = nullptr) : GameObject(parent), m_level(1) {}

    int level() const {
        return m_level;
    }

    void setLevel(int level) {
        if (m_level != level) {
            m_level = level;
            emit levelChanged();
        }
    }

signals:
    void levelChanged();

private:
    int m_level;
};