#pragma once
#include "game_object.h"

class Building : public GameObject
{
    Q_OBJECT;
    Q_PROPERTY(int level READ level WRITE setLevel NOTIFY levelChanged);

public:
    explicit Building(QObject* parent = nullptr);

    void load(const BaseData::GameObject& data) override;

    int level() const;
    void setLevel(int level);
    void setFootprint(int width, int height);

signals:
    void levelChanged();

private:
    int m_level;
};