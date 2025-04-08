#pragma once
#include <QObject>
#include "math/lib.h"

class GameObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(math::ivec2 position READ getPosition WRITE setPosition NOTIFY positionChanged);
    Q_PROPERTY(QString assetUuid READ getAssetUuid CONSTANT )

public:
    explicit GameObject(QObject *parent = nullptr);

    //propertis
    QString name() const;
    void setName(const QString &name);

    math::ivec2 getPosition() const;
    void setPosition(const math::ivec2& position);

    QString getAssetUuid();
    void setAssetUiid(const QString& uuid);


signals:
    void nameChanged();
    void positionChanged();

private:
    QString m_name;
    math::ivec2 m_position;
    QString assetUuid;
};