#include "slice_asset.h"

namespace fs = std::filesystem;

SliceAsset::SliceAsset(QObject* parent):
    SliceAsset(AssetTypes::slice, parent)
{

}

SliceAsset::SliceAsset(AssetTypes::Type type, QObject* parent):
    Asset(type, parent)
{

}

void SliceAsset::load(const BaseData::AssetData& data)
{
    Asset::load(data);

    loadAtlasFiles(data.root() / data.sliceData->thumbnail, data.root() / data.sliceData->atlas);
}

void SliceAsset::loadAtlasFiles(const std::filesystem::path& thumbnail, const std::filesystem::path& atlas)
{
    thumbnailPath = thumbnail;
    atlasPath = atlas;

    if (fs::exists(thumbnailPath))
    {
        thumbnailImage.load(QString(thumbnailPath.c_str()));
    }

    if (fs::exists(atlasPath))
    {
        atlasImage.load(QString(atlasPath.c_str()));
    }

    if (!atlasImage.isNull())
    {
        tiles = splitImageIntoTiles(atlasImage, 4, 6);
    }
    else
    {
        tiles.clear();
    }
}

QImage SliceAsset::thumbnail()
{
    // Prefer first atlas tile (fast and consistent for landscape).
    if (!tiles.empty() && !tiles[0].isNull())
    {
        return tiles[0];
    }

    // Fallback to explicit thumbnail if present.
    if (!thumbnailImage.isNull())
    {
        return thumbnailImage;
    }

    // Keep editor stable even if asset data is incomplete.
    return QImage();
}

QSize SliceAsset::getSize(DiamondIsometry* iso)
{
    diamond_dimensions dimensions = iso->getDimensions();
    float mapSize = dimensions.getCellWidth();

    QSize imageRealSize = thumbnail().size();
    if (mapSize <= 0.0f || imageRealSize.width() <= 0)
    {
        return QSize();
    }
    float k = imageRealSize.width() / mapSize;

    return imageRealSize / k;

}

void SliceAsset::registerImages(RegistationHandle handle)
{
    // Atlas-less types (cliff3d/stone3d/cyclopean3d/texture2d/tech3d) have no
    // tiles at all, so index 0 — what the palette asks for — has to come from
    // the explicit thumbnail, otherwise their cells render blank.
    if (tiles.empty())
    {
        handle(0, thumbnailImage);
        return;
    }

    for(int i=0; i<tiles.size(); ++i)
    {
        if (!tiles[i].isNull())
        {
            handle(i, tiles[i]);
        }
    }
}

TileSet::TileType SliceAsset::subTileTypeByIndex(size_t tileIndex)
{
    switch (tileIndex)
    {
    case 0:
    case 1:
    case 2:
    case 3:
        return TileSet::Full;

    case 4:
        return TileSet::DownLack;
    case 5:
        return TileSet::LeftLack;
    case 6:
        return TileSet::UpLack;
    case 7:
        return TileSet::RightLack;

    case 8:
        return TileSet::UpCorner;
    case 9:
        return TileSet::RightCorner;
    case 10:
        return TileSet::DownCorner;
    case 11:
        return TileSet::LeftCorner;

    case 12:
    case 16:
        return TileSet::RightUpLine;
    
    case 13:
    case 17:
        return TileSet::RightDownLine;
    
    case 14:
    case 18:
        return TileSet::LeftDownLine;

    case 15:
    case 19:
        return TileSet::LeftUpLine;

    case 20:
        return TileSet::UpAndDownCorners;

    case 21:
        return TileSet::LeftRightCorners;

    default:
        break;
    }

    return TileSet::Unknown;
}

size_t SliceAsset::subTileIndexByType(TileSet::TileType type) const
{
    switch (type)
    {
    case TileSet::Full:
        return 0;

    case TileSet::DownLack:
        return 4;
    case TileSet::LeftLack:
        return 5;
    case TileSet::UpLack:
        return 6;
    case TileSet::RightLack:
        return 7;

    case TileSet::UpCorner:
        return 8;
    case TileSet::RightCorner:
        return 9;
    case TileSet::DownCorner:
        return 10;
    case TileSet::LeftCorner:
        return 11;

    case TileSet::RightUpLine:
        return 12;

    case TileSet::RightDownLine:
        return 13;

    case TileSet::LeftDownLine:
        return 14;

    case TileSet::LeftUpLine:
        return 15;

    case TileSet::UpAndDownCorners:
        return 20;

    case TileSet::LeftRightCorners:
        return 21;

    default:
        break;
    }

    return 22;
}

QString SliceAsset::getUrlInternal() const
{
    QString url = QString("image://assetImages/") + uuid().toString(QUuid::WithoutBraces);

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
