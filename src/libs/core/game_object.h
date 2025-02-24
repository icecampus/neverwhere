#pragma once
#include <QObject>

class GameObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)

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

signals:
    void nameChanged();

private:
    QString m_name;
};