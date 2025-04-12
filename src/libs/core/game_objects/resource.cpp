#include "resource.h"


Resource::Resource(QObject* parent): 
    GameObject(GameObjectTypes::Resource, parent),
    m_quantity(0) 
{

}

int Resource::quantity() const 
{
    return m_quantity;
}

void Resource::setQuantity(int quantity) 
{
    if (m_quantity != quantity) 
    {
        m_quantity = quantity;
        emit quantityChanged();
    }
}
