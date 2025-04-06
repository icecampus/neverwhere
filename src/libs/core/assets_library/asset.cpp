#include "asset.h"


Asset::Asset(QObject* parent)
    : QObject(parent)
{
}

QUuid Asset::uuid() const 
{ 
    return m_uuid; 
}

QString Asset::name() const 
{ 
    return m_name; 
}
