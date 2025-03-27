#include "layer.h"

Layer::Layer(QString name_, QObject* parent):
    QObject(parent),
    _name(name_)
{

}

QString Layer::name() const 
{ 
    return _name; 
}

//LayersModel
LayersLibrary::LayersLibrary(QObject* parent)
{

}
