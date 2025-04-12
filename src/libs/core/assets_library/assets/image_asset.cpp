#include "image_asset.h"

namespace fs = std::filesystem;

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

QSize ImageAsset::getSize(StaggeredIsometry* iso)
{
    staggered_dimensions dimensions = iso->getDimensions();
    float mapSize = dimensions.getCellWidth() * m_width;

    QSize imageRealSize = thumbnail().size();
    float k = imageRealSize.width() / mapSize;

    return imageRealSize / k;
}

QString ImageAsset::getUrlInternal() const
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

    if (fs::exists(imagePath))
    {
        image.load(QString(imagePath.c_str()));
    }
}

QImage ImageAsset::thumbnail()
{
    return image;
}

nlohmann::json ImageAsset::save()
{
    nlohmann::json graphics = {
    {"width", m_width},
    {"imageFilename", m_imageFilename.toStdString()}
    };

    nlohmann::json j = {
        {"uuid", _uuid.toString(QUuid::WithoutBraces).toStdString()},
        {"name", _name.toStdString()},
        {"graphics", graphics}
    };

    return j;
}

