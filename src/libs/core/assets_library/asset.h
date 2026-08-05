#pragma once
#include <QObject>
#include <QUuid>
#include <QJsonObject>
#include <QImage>
#include <QFileInfo>
#include <nlohmann/json.hpp>
#include <filesystem>
#include "base_data/lib.h"

#include "base.h"

class Asset : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUuid uuid READ uuid CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString thumbnailUrl READ getThumbnailUrl CONSTANT)
    Q_PROPERTY(LayerTypes::Type layerType READ getLayerType CONSTANT)
    Q_PROPERTY(AssetTypes::Type type READ getType CONSTANT)
    // Boolean mirror for QML conditionals (enum namespace values like
    // AssetTypes.cliff3d are not reliably accessible in QML here).
    Q_PROPERTY(bool isCliff3d READ getIsCliff3d CONSTANT)
    Q_PROPERTY(bool isCyclopean3d READ getIsCyclopean3d CONSTANT)
    Q_PROPERTY(bool isStone3d READ getIsStone3d CONSTANT)
    Q_PROPERTY(bool isTexture2d READ getIsTexture2d CONSTANT)

    // pivot - это смещение ЦЕНТРА картинки относительно центра клетки игрового поля в размере клеток игрового поля
    // (0,0) - нет смешения, 
    // (1,1) смещение на ширину одной клетки вправо и на высоту(!) одной клетки вниз, 
    // (2, -2) на две вправо и на 2 высоты клетки вверх
    // высота или шира определяются топологией карты
    Q_PROPERTY(math::vec2 pivot READ getPivot WRITE setPivot NOTIFY pivotChanged)

    Q_PROPERTY(bool editMode READ getEditMode WRITE setEditMode NOTIFY editModeChanged)

    public:
    using RegistationHandle = std::function<void(int index, const QImage& image)>;
    explicit Asset(AssetTypes::Type type, QObject* parent);

    //properties
    QUuid uuid() const;
    QString name() const;
    QString getThumbnailUrl() const;
    LayerTypes::Type getLayerType() const;
    AssetTypes::Type getType() const { return type; }
    bool getIsCliff3d() const { return type == AssetTypes::cliff3d; }
    bool getIsCyclopean3d() const { return type == AssetTypes::cyclopean3d; }
    bool getIsStone3d() const { return type == AssetTypes::stone3d; }
    bool getIsTexture2d() const { return type == AssetTypes::texture2d; }

    math::vec2 getPivot() const;
    void setPivot(const math::vec2& pivot);

    bool getEditMode() const;
    void setEditMode(bool state);
    //
    virtual void load(const BaseData::AssetData& data);
    virtual QImage thumbnail() = 0;

    virtual void registerImages(RegistationHandle handle)=0;
    
    const AssetTypes::Type type;
    const BaseData::AssetData& getData();

signals:
    void pivotChanged();
    void editModeChanged();

protected:
    virtual QString getUrlInternal() const=0;

    BaseData::AssetData data;

    bool editMode{false};
    
};