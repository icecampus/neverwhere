#include "asset.h"
#include <magic_enum/magic_enum.hpp>


using json = nlohmann::json;
namespace fs = std::filesystem;

Asset::Asset(AssetTypes::Type type_, QObject* parent):
    QObject(parent),
    type(type_)
{
}

QUuid Asset::uuid() const 
{ 
    return _uuid; 
}

QString Asset::name() const 
{ 
    return _name; 
}

QString Asset::getThumbnailUrl() const
{
    return getUrlInternal();
}

LayerTypes::Type Asset::getLayerType() const
{
    return _layerType;
}

void Asset::load(const std::filesystem::path& indexPath, const nlohmann::json& j)
{
    _uuid = QUuid::fromString(QString::fromStdString(j["uuid"].get<std::string>()));
    _name = QString::fromStdString(indexPath.parent_path().stem().string());

    std::string layerTypeStr = j["layerType"];
    auto layerType = magic_enum::enum_cast<LayerTypes::Type>(layerTypeStr, magic_enum::case_insensitive);
    
    if (layerType.has_value())
    {
        _layerType = layerType.value();
    }
    else
    {
        qWarning() << "Undefined layer type for asset: " << _name;
    }


}
