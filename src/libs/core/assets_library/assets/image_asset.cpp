#include "image_asset.h"

namespace fs = std::filesystem;

ImageAsset::ImageAsset(QObject* parent):
    Asset(AssetTypes::image, parent)
{
    //m_image.load(QString::fromStdString(imagePath.string()));

}

float ImageAsset::getWidth() const 
{ 
    return widthInCells; 
}

void ImageAsset::setWidth(float widthInCells_)
{
    //spdlog::info("setWidth: {}",  widthInCells_);
    if(widthInCells != widthInCells_)
    {
        widthInCells = widthInCells_;
        emit widthChanged();
    }
}

QString ImageAsset::getImageFilename() const 
{ 
    return imageFilename; 
}

QSize ImageAsset::getSize(StaggeredIsometry* iso)
{
    staggered_dimensions dimensions = iso->getDimensions();
    float mapSize = dimensions.getCellWidth() * widthInCells;

    QSize imageRealSize = thumbnail().size();
    float k = imageRealSize.width() / mapSize;

    return imageRealSize / k;
}

void ImageAsset::setSize(const math::vec2& screenSize, StaggeredIsometry* iso)
{
   widthInCells = screenSize.x /  iso->dimensions.getCellWidth();
   emit widthChanged();
}

QString ImageAsset::getUrlInternal() const
{
    QString url = QString("image://assetImages/") + _uuid.toString(QUuid::WithoutBraces);

    return url;
}


void ImageAsset::load(const std::filesystem::path& indexPath,  const nlohmann::json& j)
{
    Asset::load(indexPath, j);

    widthInCells = j["image"]["width"].get<int>();
    imageFilename = QString::fromStdString(j["image"]["imageFilename"].get<std::string>());

    imagePath = indexPath.parent_path() / imageFilename.toStdString();

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
    {"width", widthInCells},
    {"imageFilename", imageFilename.toStdString()}
    };

    nlohmann::json j = {
        {"uuid", _uuid.toString(QUuid::WithoutBraces).toStdString()},
        {"name", _name.toStdString()},
        {"graphics", graphics}
    };

    return j;
}

void ImageAsset::registerImages(RegistationHandle handle)
{
    handle(0, image);
}

