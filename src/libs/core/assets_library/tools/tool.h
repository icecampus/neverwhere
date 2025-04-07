#pragma once
#include <QObject>
#include "simple_model.h"

//Tool
class Tool : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString icon READ icon CONSTANT)
public:
    explicit Tool(const QString& name, const QString& icon, QObject* parent);

    QString name() const;
    QString icon() const;

private:
    QString _name;
    QString _icon;

};

