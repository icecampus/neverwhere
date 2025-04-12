#include "slice_asset.h"

namespace fs = std::filesystem;

SliceAsset::SliceAsset(QObject* parent):
    Asset(AssetTypes::slice, parent)
{

}

void SliceAsset::load(const std::filesystem::path& indexPath, const nlohmann::json& j)
{
    Asset::load(indexPath, j);

    thumbnailFilename = j["slice"]["thumbnail"].get<std::string>();
    thumbnailPath = indexPath.parent_path() / thumbnailFilename;

    atlasFilename = j["slice"]["atlas"].get<std::string>();
    atlasPath = indexPath.parent_path() / atlasFilename;

    if (fs::exists(thumbnailPath))
    {
        thumbnailImage.load(QString(thumbnailPath.c_str()));
    }

    if (fs::exists(atlasPath))
    {
        atlasImage.load(QString(atlasPath.c_str()));
    }

    tiles = splitImageIntoTiles(atlasImage, 4, 6);
}

QImage SliceAsset::thumbnail()
{
    return tiles[0];
}

QSize SliceAsset::getSize(StaggeredIsometry* iso)
{
    staggered_dimensions dimensions = iso->getDimensions();
    float mapSize = dimensions.getCellWidth();

    QSize imageRealSize = thumbnail().size();
    float k = imageRealSize.width() / mapSize;

    return imageRealSize / k;

}

QString SliceAsset::getUrlInternal() const
{
    QString url = QString("image://assetImages/") + _uuid.toString(QUuid::WithoutBraces);

    return url;
}

std::vector<QImage> SliceAsset::splitImageIntoTiles(const QImage& sourceImage, int cols, int rows)
{
    if (sourceImage.isNull()) 
    {
        qDebug() << "Try split empty image";
        return {};
    }

    int width = sourceImage.width();
    int height = sourceImage.height();

    int tileWidth = width / cols;
    int tileHeight = height / rows;

    std::vector<QImage> tiles;

    for (int y = 0; y < rows; ++y) 
    {
        for (int x = 0; x < cols; ++x) 
        {
            QRect rect(
                x * tileWidth,    
                y * tileHeight,   
                tileWidth,        
                tileHeight
            );
            
            QImage tile = sourceImage.copy(rect);
            tiles.push_back(tile);
        }
    }

    return tiles;
}
