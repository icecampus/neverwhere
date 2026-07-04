#include "image_asset.h"

namespace fs = std::filesystem;

ImageAsset::ImageAsset(QObject* parent):
    Asset(AssetTypes::image, parent)
{
    //m_image.load(QString::fromStdString(imagePath.string()));

}

float ImageAsset::getWidth() const 
{ 
    return data.imageData->width; 
}

void ImageAsset::setWidth(float widthInCells)
{
    //spdlog::info("setWidth: {}",  widthInCells_);
    if(data.imageData->width != widthInCells)
    {
        data.imageData->width = widthInCells;
        emit widthChanged();
    }
}

QString ImageAsset::getImageFilename() const 
{ 
    return data.imageData->imageFilename.c_str(); 
}

QSize ImageAsset::getScreenSize(DiamondIsometry* iso)
{
    diamond_dimensions dimensions = iso->getDimensions();
    float mapSize = dimensions.getCellWidth() * data.imageData->width;

    QSize imageRealSize = thumbnail().size();
    float k = imageRealSize.width() / mapSize;

    return imageRealSize / k;
}

void ImageAsset::setScreenWidth(const float screenWidth, DiamondIsometry* iso)
{
   //spdlog::info("screenWidth: {}",  screenWidth);
   data.imageData->width = screenWidth /  iso->dimensions.getCellWidth();
   emit widthChanged();
}

QString ImageAsset::getUrlInternal() const
{
    QString url = QString("image://assetImages/") +  uuid().toString(QUuid::WithoutBraces);

    return url;
}


void ImageAsset::load(const BaseData::AssetData& data)
{
    Asset::load(data);

    imagePath = data.root() / data.imageData->imageFilename;

    if (fs::exists(imagePath))
    {
        image.load(QString(imagePath.c_str()));
    }
}

QImage ImageAsset::thumbnail()
{
    return image;
}

void ImageAsset::registerImages(RegistationHandle handle)
{
    handle(0, image);
}

