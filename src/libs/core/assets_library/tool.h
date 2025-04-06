#pragma once
#include <QObject>
#include "simple_model.h"

//Tool
class Tool : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
public:
    explicit Tool(const QString& name, QObject* parent = nullptr);

    QString name() const;

private:
    QString _name;

};

//ToolsModel
class ToolsModel: public SimpleModel<Tool>
{
    Q_OBJECT
public:
    explicit ToolsModel(QObject* parent = nullptr);

};

