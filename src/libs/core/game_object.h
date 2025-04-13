#pragma once
#include <QObject>
#include <QUuid>
#include "math/lib.h"
#include "base.h"

namespace GameObjectTypes
{
    Q_NAMESPACE;
    enum Type
    {
        Tile2D,
        Landscape,
        Resource,
        Buildings,
        Cloud
    };
    Q_ENUM_NS(Type);
}

//GameObject
class GameObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(math::ivec2 position READ getPosition WRITE setPosition NOTIFY positionChanged);
    Q_PROPERTY(QUuid assetUuid READ getAssetUuid CONSTANT )
    Q_PROPERTY(GameObjectTypes::Type type READ getType CONSTANT)

public:
    explicit GameObject(GameObjectTypes::Type type, QObject *parent = nullptr);

    //propertis
    QString name() const;
    void setName(const QString &name);

    math::ivec2 getPosition() const;
    void setPosition(const math::ivec2& position);

    QUuid getAssetUuid();
    void setAssetUiid(const QUuid& uuid);

    GameObjectTypes::Type getType();

signals:
    void nameChanged();
    void positionChanged();

private:
    QString m_name;
    math::ivec2 m_position;
    QUuid assetUuid;
    GameObjectTypes::Type type;
};