#include "image_asset.h"

ImageAsset::ImageAsset(QObject* parent):
    Asset(AssetTypes::image, parent)
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
    QString url = QString("image://assetImages/") + _uuid.toString(QUuid::WithoutBraces);

    return url;
}


void ImageAsset::load(const std::filesystem::path& indexPath,  const nlohmann::json& j)
{
    Asset::load(indexPath, j);

    m_width = j["image"]["width"].get<int>();
    m_imageFilename = QString::fromStdString(j["image"]["imageFilename"].get<std::string>());

    imagePath = indexPath.parent_path() / m_imageFilename.toStdString();
}

QImage ImageAsset::thumbnail()
{
    QImage result;
    if (fs::exists(imagePath))
    {
        result.load(QString(imagePath.c_str()));
    }

    return result;
}

nlohmann::json ImageAsset::save()
{
    json graphics = {
    {"width", m_width},
    {"imageFilename", m_imageFilename.toStdString()}
    };

    json j = {
        {"uuid", _uuid.toString(QUuid::WithoutBraces).toStdString()},
        {"name", m_name.toStdString()},
        {"graphics", graphics}
    };

    return j;
}
