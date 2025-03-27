#include "chapters_model.h"

Chapter::Chapter(QString name_, QObject* parent) :
    QObject(parent),
    _name(name_)
{

}

QString Chapter::name() const
{
    return _name;
}


//LayersModel
ChaptersModel::ChaptersModel(QObject* parent)
{

}
