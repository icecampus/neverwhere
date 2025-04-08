#include "asset.h"


Asset::Asset(AssetTypes type_, QObject* parent):
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

void Asset::load(const std::filesystem::path& indexPath, const nlohmann::json& j)
{
    _uuid = QUuid::fromString(QString::fromStdString(j["uuid"].get<std::string>()));
    _name = QString::fromStdString(indexPath.parent_path().stem().string());
}
