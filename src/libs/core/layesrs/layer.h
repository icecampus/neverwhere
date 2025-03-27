#pragma once
#include <QObject>
#include "simple_model.h"

//Layer
class Layer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)

public:
    explicit Layer(QString name, QObject* parent = nullptr);

    QString name() const;

private:
    QString _name;
};


//LayersModel
class LayersLibrary : public SimpleModel<Layer>
{
    Q_OBJECT
public:
    explicit LayersLibrary(QObject* parent = nullptr);

};

