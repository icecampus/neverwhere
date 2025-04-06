#include "asset.h"


Asset::Asset(QObject* parent)
    : QObject(parent)
{
}

QUuid Asset::uuid() const 
{ 
    return m_uuid; 
}

QString Asset::name() const 
{ 
    return m_name; 
}

void Asset::load(const std::filesystem::path& indexPath, const nlohmann::json& j)
{
    m_uuid = QUuid::fromString(QString::fromStdString(j["uuid"].get<std::string>()));
    m_name = QString::fromStdString(indexPath.parent_path().stem().string());
}
