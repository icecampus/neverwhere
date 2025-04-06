#include "image_asset.h"

ImageAsset::ImageAsset(QObject* parent):
    Asset(parent)
{
    //m_image.load(QString::fromStdString(imagePath.string()));

}

int ImageAsset::width() const 
{ 
    return m_width; 
}

QString ImageAsset::imageFilename() const 
{ 
    return m_imageFilename; 
}

QString ImageAsset::getUrl() const
{
    QString url = QString("image://assetImages/") + m_uuid.toString(QUuid::WithoutBraces);

    return url;
}


void ImageAsset::load(const std::filesystem::path& indexPath,  const nlohmann::json& j)
{
    m_uuid = QUuid::fromString(QString::fromStdString(j["uuid"].get<std::string>()));
    m_name = QString::fromStdString(indexPath.parent_path().stem().string());
    m_width = j["graphics"]["width"].get<int>();
    m_imageFilename = QString::fromStdString(j["graphics"]["imageFilename"].get<std::string>());

    fs::path imagePath = indexPath.parent_path() / j["graphics"]["imageFilename"].get<std::string>();

    
}

nlohmann::json ImageAsset::save()
{
    json graphics = {
    {"width", m_width},
    {"imageFilename", m_imageFilename.toStdString()}
    };

    json j = {
        {"uuid", m_uuid.toString(QUuid::WithoutBraces).toStdString()},
        {"name", m_name.toStdString()},
        {"graphics", graphics}
    };

    return j;
}
