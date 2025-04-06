#include "tool.h"

Tool::Tool(const QString& name_, QObject* parent /*= nullptr*/):
    QObject(parent),
    _name(name_)
{

}

QString Tool::name() const
{
    return _name;
}

//ToolsModel
ToolsModel::ToolsModel(QObject* parent /*= nullptr*/)
{

}
