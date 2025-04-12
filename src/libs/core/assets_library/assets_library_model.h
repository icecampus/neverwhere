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

    Q_INVOKABLE Asset* getAsset(const QUuid& uuid);

protected:
    void processElement(AssetsPackModel& element) override;

    std::map<QUuid, Asset*> uuid2Asset;
};

