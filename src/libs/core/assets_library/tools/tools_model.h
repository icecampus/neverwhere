#pragma once
#include <QObject>
#include "simple_model.h"
#include "tool.h"
#include "assets_library/asset.h"
#include "map/map_model.h"
#include "topology/staggered_isometry.h"

//AssetToolsModel
class AssetToolsModel : public SimpleModel<Tool>
{
    Q_OBJECT
    Q_PROPERTY(int currentTool READ getCurrentTool WRITE setCurrentTool NOTIFY currentToolChanged)
public:
    explicit AssetToolsModel(QObject* parent);
    
    int getCurrentTool() const;
    void setCurrentTool(int currentIndex);

    void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, StaggeredIsometryView* iso);

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

    Q_INVOKABLE void click(QPoint screenPos, MapModel* mapModel, StaggeredIsometryView* iso);
signals:
    void currentAssetChanged();
    void toolsModelChanged();

private:
    Asset* currentAsset{nullptr};

    std::unordered_map<AssetTypes::Type, std::unique_ptr<AssetToolsModel>> assetType2ToolsModel;
};