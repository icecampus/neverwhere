#pragma once
#include <QObject>
#include "simple_model.h"
#include "tool.h"
#include "assets_library/asset.h"

//AssetToolsModel
class AssetToolsModel : public SimpleModel<Tool>
{
    Q_OBJECT
public:
    explicit AssetToolsModel(QObject* parent);

};


//ToolsModel
class ToolsModel: public QObject
{
    Q_OBJECT
    Q_PROPERTY(Asset* currentAsset READ getCurrentAsset WRITE setCurrentAsset NOTIFY currentAssetChanged)
    Q_PROPERTY(AssetToolsModel* toolsModel READ getToolsModel NOTIFY toolsModelChanged)
public:
    explicit ToolsModel(QObject* parent);

    Asset* getCurrentAsset();
    void setCurrentAsset(Asset* asset);

    AssetToolsModel* getToolsModel();

signals:
    void currentAssetChanged();
    void toolsModelChanged();

private:
    Asset* currentAsset{nullptr};

    AssetToolsModel tmpToolsMode;

};