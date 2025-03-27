#pragma once
#include "simple_model.h"

struct Chapter: public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)
public:
    explicit Chapter(QString name, QObject* parent = nullptr);

    QString name() const;
private:
    QString _name;
};

//LayersModel
class ChaptersModel : public SimpleModel<Chapter>
{
    Q_OBJECT
public:
    explicit ChaptersModel(QObject* parent = nullptr);

};