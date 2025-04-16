#pragma once
#include "game_object.h"


class Resource : public GameObject
{
    Q_OBJECT
    Q_PROPERTY(int quantity READ quantity WRITE setQuantity NOTIFY quantityChanged)

public:
    explicit Resource(QObject* parent = nullptr);

    void load(const BaseData::GameObject& data);

    int quantity() const;
    void setQuantity(int quantity);

signals:
    void quantityChanged();

private:
    int m_quantity;
};