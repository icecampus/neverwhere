#include "asset.h"


Asset::Asset(const std::filesystem::path& indexPath, const json& j, const std::string& assetPath, QObject* parent)
    : QObject(parent),
    m_uuid(QUuid::fromString(QString::fromStdString(j["uuid"].get<std::string>()))),
    m_name(QString::fromStdString(indexPath.parent_path().stem().string())),
    m_width(j["graphics"]["width"].get<int>()),
    m_imageFilename(QString::fromStdString(j["graphics"]["imageFilename"].get<std::string>()))
{
    fs::path imagePath = indexPath.parent_path() / j["graphics"]["imageFilename"].get<std::string>();
    m_image.load(QString::fromStdString(imagePath.string()));
}

QString Asset::getUrl() const
{
    QString url = QString("image://assetImages/") + m_uuid.toString(QUuid::WithoutBraces);

    return url;
}
