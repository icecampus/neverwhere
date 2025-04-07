#pragma once
#include <QObject>
#include "simple_model.h"
#include "tool.h"

//ToolsModel
class ToolsModel : public SimpleModel<Tool>
{
    Q_OBJECT
public:
    explicit ToolsModel(QObject* parent = nullptr);

};

