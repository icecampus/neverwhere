#pragma once
#include <QObject>
#include "math/lib.h"

class GameObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(math::ivec2 position READ getPosition WRITE setPosition NOTIFY positionChanged);

public:
    explicit GameObject(QObject *parent = nullptr) : QObject(parent) {}

    QString name() const {
        return m_name;
    }

    void setName(const QString &name) {
        if (m_name != name) {
            m_name = name;
            emit nameChanged();
        }
    }

    math::ivec2 getPosition() const {
        return m_position;
    }

    void setPosition(const math::ivec2& position) {
        if (m_position != position) {
            m_position = position;
            emit positionChanged();
        }
    }
signals:
    void nameChanged();
    void positionChanged();

private:
    QString m_name;
    math::ivec2 m_position;
};