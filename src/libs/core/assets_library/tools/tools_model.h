#pragma once
#include <QObject>
#include <QVariantMap>
#include "simple_model.h"
#include "tool.h"
#include "assets_library/asset.h"
#include "map/map_model.h"
#include "topology/diamond_isometry.h"

//AssetToolsModel
class AssetToolsModel : public SimpleModel<Tool>
{
    Q_OBJECT
    Q_PROPERTY(int currentTool READ getCurrentTool WRITE setCurrentTool NOTIFY currentToolChanged)
public:
    explicit AssetToolsModel(QObject* parent);

    int getCurrentTool() const;
    void setCurrentTool(int currentIndex);

    void click(QPoint screenPos, Asset* currentAsset, LayerModel* mapModel, DiamondIsometryView* iso,
        bool ctrlModifier, bool shiftModifier, bool altModifier);

    void stroke(StrokeKind kind, QPoint screenPos, Asset* currentAsset, LayerModel* mapModel,
        DiamondIsometryView* iso, bool ctrlModifier, bool shiftModifier, bool altModifier);

    void keyPress(int key, Asset* currentAsset, LayerModel* mapModel);

signals:
    void currentToolChanged();

private:
    int currentTool = 0;
};


//AssetToolsSelector
class FencePencil;
class AssetToolsSelector: public QObject
{
    Q_OBJECT
    Q_PROPERTY(Asset* currentAsset READ getCurrentAsset WRITE setCurrentAsset NOTIFY currentAssetChanged)
    Q_PROPERTY(AssetToolsModel* toolsModel READ getToolsModel NOTIFY toolsModelChanged)
    // Transient fence-tool state for MapRenderItem (ghost preview pieces +
    // selection): {selectedFenceId, valid, assetUuid, pieces:[{x,y,kind,
    // axisX,axisY,length}]}; empty when no fence3d asset is selected.
    Q_PROPERTY(QVariantMap fenceToolState READ getFenceToolState NOTIFY fenceToolStateChanged)
public:
    explicit AssetToolsSelector(QObject* parent = nullptr);

    Asset* getCurrentAsset();
    void setCurrentAsset(Asset* asset);

    AssetToolsModel* getToolsModel();
    QVariantMap getFenceToolState() const;

    Q_INVOKABLE void click(QPoint screenPos, MapModel* mapModel, DiamondIsometryView* iso, bool ctrlModifier, bool shiftModifier, bool altModifier);
    Q_INVOKABLE void stroke(int kind, QPoint screenPos, MapModel* mapModel, DiamondIsometryView* iso, bool ctrlModifier, bool shiftModifier, bool altModifier);
    Q_INVOKABLE void keyPress(int key, MapModel* mapModel);
signals:
    void currentAssetChanged();
    void toolsModelChanged();
    void fenceToolStateChanged();

private:
    Asset* currentAsset{nullptr};

    std::unordered_map<AssetTypes::Type, std::unique_ptr<AssetToolsModel>> assetType2ToolsModel;
    FencePencil* fencePencil{nullptr}; // owned by assetType2ToolsModel[fence3d]
};