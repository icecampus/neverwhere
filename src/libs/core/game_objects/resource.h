#pragma once
#include "game_object.h"


class Resource : public GameObject
{
    Q_OBJECT
        Q_PROPERTY(int quantity READ quantity WRITE setQuantity NOTIFY quantityChanged)

public:
    explicit Resource(QObject* parent = nullptr) : GameObject(parent), m_quantity(0) {}

    int quantity() const {
        return m_quantity;
    }

    void setQuantity(int quantity) {
        if (m_quantity != quantity) {
            m_quantity = quantity;
            emit quantityChanged();
        }
    }

signals:
    void quantityChanged();

private:
    int m_quantity;
};