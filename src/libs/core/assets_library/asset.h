#pragma once
#include <QObject>
#include <QUuid>
#include <QJsonObject>
#include <QImage>
#include <QFileInfo>
#include <nlohmann/json.hpp>
#include <filesystem>

#include "base.h"

namespace AssetTypes
{
    Q_NAMESPACE;
    enum Type
    {
        image,
        slice
    };
    Q_ENUM_NS(Type);
}

class Asset : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUuid uuid READ uuid CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString thumbnailUrl READ getThumbnailUrl CONSTANT)
    Q_PROPERTY(LayerTypes::Type layerType READ getLayerType CONSTANT)

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

    math::vec2 getPivot() const;
    void setPivot(const math::vec2& pivot);

    bool getEditMode() const;
    void setEditMode(bool state);
    //
    virtual void load(const std::filesystem::path& indexPath,  const nlohmann::json& j);
    virtual QImage thumbnail() = 0;

    virtual void registerImages(RegistationHandle handle)=0;
    
    const AssetTypes::Type type;

signals:
    void pivotChanged();
    void editModeChanged();

protected:
    virtual QString getUrlInternal() const=0;

    std::filesystem::path indexPath;
    QUuid _uuid;           
    QString _name;   
    LayerTypes::Type _layerType{ LayerTypes::Decoration };
    math::vec2 pivot{ 0.5, -1.0f };

    bool editMode{false};
    
};