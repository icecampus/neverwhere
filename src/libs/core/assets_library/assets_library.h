#pragma once
#include <QObject>
#include "simple_model.h"
#include "asset_pack.h"

//Layer


//LayersModel
class AssetsLibrary : public SimpleModel<AssetPack>
{
    Q_OBJECT
public:
    explicit AssetsLibrary(QObject* parent = nullptr);

};

