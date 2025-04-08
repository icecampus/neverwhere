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
    return m_name; 
}

void Asset::load(const std::filesystem::path& indexPath, const nlohmann::json& j)
{
    _uuid = QUuid::fromString(QString::fromStdString(j["uuid"].get<std::string>()));
    m_name = QString::fromStdString(indexPath.parent_path().stem().string());
}
