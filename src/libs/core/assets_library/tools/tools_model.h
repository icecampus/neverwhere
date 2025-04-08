#pragma once
#include <QObject>
#include "simple_model.h"
#include "tool.h"
#include "assets_library/asset.h"

//AssetToolsModel
class AssetToolsModel : public SimpleModel<Tool>
{
    Q_OBJECT
    Q_PROPERTY(int currentTool READ getCurrentTool WRITE setCurrentTool NOTIFY currentToolChanged)
public:
    explicit AssetToolsModel(QObject* parent);
    
    int getCurrentTool() const;
    void setCurrentTool(int currentIndex);

signals:
    void currentToolChanged();

private:
    int currentTool = 0;
};


//AssetToolsSelector
class AssetToolsSelector: public QObject
{
    Q_OBJECT
    Q_PROPERTY(Asset* currentAsset READ getCurrentAsset WRITE setCurrentAsset NOTIFY currentAssetChanged)
    Q_PROPERTY(AssetToolsModel* toolsModel READ getToolsModel NOTIFY toolsModelChanged)
public:
    explicit AssetToolsSelector(QObject* parent = nullptr);

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