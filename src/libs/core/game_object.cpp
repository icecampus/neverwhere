#include "game_object.h"


GameObject::GameObject(GameObjectTypes::Type type_, QObject* parent) 
    : QObject(parent),
    type(type_)
{

}

QString GameObject::name() const 
{
    return m_name;
}

void GameObject::setName(const QString& name) 
{
    if (m_name != name) 
    {
        m_name = name;
        emit nameChanged();
    }
}

math::ivec2 GameObject::getPosition() const 
{
    return m_position;
}

void GameObject::setPosition(const math::ivec2& position) 
{
    if (m_position != position) 
    {
        m_position = position;
        emit positionChanged();
    }
}

QUuid GameObject::getAssetUuid()
{
    return assetUuid;
}

void GameObject::setAssetUiid(const QUuid& uuid)
{
    assetUuid = uuid;
}

GameObjectTypes::Type GameObject::getType()
{
    return type;
}
