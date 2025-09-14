#include "game_object.h"
#include "base.h"


GameObject::GameObject(GameObjectTypes::Type type, QObject* parent): 
    QObject(parent)
{
    data.type = type;
}

void GameObject::load(const BaseData::GameObject& data_)
{
    assert(data.type == data_.type );
    data = data_;
    emit positionChanged();
}

BaseData::GameObject GameObject::getData() const
{
    return data;
}

QString GameObject::name() const 
{
    return _name;
}

void GameObject::setName(const QString& name) 
{
    if (_name != name) 
    {
        _name = name;
        emit nameChanged();
    }
}

math::ivec2 GameObject::getPosition() const 
{
    return data.position;
}

void GameObject::setPosition(const math::ivec2& position) 
{
    if (data.position != position) 
    {
        data.position = position;
        emit positionChanged();
    }
}

QUuid GameObject::getAssetUuid() const
{
    return base::boostUuidToQUuid(data.assetUuid);
}

void GameObject::setAssetUiid(const QUuid& uuid)
{
     data.assetUuid = base::QUuidToBoostUuid(uuid);
}

GameObjectTypes::Type GameObject::getType()
{
    return data.type;
}
