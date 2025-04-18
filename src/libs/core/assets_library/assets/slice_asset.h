#pragma once
#include "assets_library/asset.h"
#include <array>
#include "topology/staggered_isometry.h"
#include "topology/staggered_tiled_landscape.h"

class SliceAsset: public Asset
{
    Q_OBJECT

    
public:
    explicit SliceAsset(QObject* parent);

    //properties
    void load(const BaseData::AssetData& data) override;
    QImage thumbnail() override;

    //INVOKABLE 
    Q_INVOKABLE QSize getSize(StaggeredIsometry* iso);

    //
    void registerImages(RegistationHandle handle) override;

    //
    TileSet::TileType subTileTypeByIndex(size_t tileIndex) const;
    size_t subTileIndexByType(TileSet::TileType type) const;
    
protected:
    QString getUrlInternal() const override;

private:
    static std::vector<QImage> splitImageIntoTiles(const QImage& source, int cols, int rows);

    std::filesystem::path thumbnailPath;
    QImage thumbnailImage;

    std::filesystem::path atlasPath;
    QImage atlasImage;

    std::vector<QImage> tiles;
};
