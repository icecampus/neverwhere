#pragma once
#include <QObject>
#include "simple_model.h"
#include "assets_pack_model.h"

//Layer


//LayersModel
class AssetsLibraryModel : public SimpleModel<AssetsPackModel>
{
    Q_OBJECT
public:
    explicit AssetsLibraryModel(QObject* parent = nullptr);

};

