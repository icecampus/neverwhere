#pragma once
#include <QObject>
#include <QUuid>
#include "math/lib.h"
#include "base.h"
#include "base_data/lib.h"

//GameObject
class GameObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(math::ivec2 position READ getPosition WRITE setPosition NOTIFY positionChanged);
    Q_PROPERTY(QUuid assetUuid READ getAssetUuid CONSTANT )
    Q_PROPERTY(GameObjectTypes::Type type READ getType CONSTANT)

public:
    explicit GameObject(GameObjectTypes::Type type, QObject *parent);

    BaseData::GameObject getData() const;

    //propertis
    QString name() const;
    void setName(const QString &name);

    math::ivec2 getPosition() const;
    void setPosition(const math::ivec2& position);

    QUuid getAssetUuid() const;
    void setAssetUiid(const QUuid& uuid);

    GameObjectTypes::Type getType();

signals:
    void nameChanged();
    void positionChanged();

protected:
    virtual void load(const BaseData::GameObject& data);

    BaseData::GameObject data;
private:
    
    QString _name;
    

};