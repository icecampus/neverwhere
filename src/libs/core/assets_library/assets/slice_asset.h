#pragma once
#include "assets_library/asset.h"
#include <array>
#include "topology/diamond_isometry.h"
#include "topology/diamond_tiled_landscape.h"

class SliceAsset: public Asset
{
    Q_OBJECT


public:
    explicit SliceAsset(QObject* parent);

    //properties
    void load(const BaseData::AssetData& data) override;
    QImage thumbnail() override;

    //INVOKABLE
    Q_INVOKABLE QSize getSize(DiamondIsometry* iso);

    //
    void registerImages(RegistationHandle handle) override;

    //
    static TileSet::TileType subTileTypeByIndex(size_t tileIndex);
    size_t subTileIndexByType(TileSet::TileType type) const;

protected:
    // For subclasses carrying their own AssetTypes value (Shape3dAsset).
    SliceAsset(AssetTypes::Type type, QObject* parent);

    // Loads thumbnail/atlas images and splits the atlas (4x6) into tiles.
    void loadAtlasFiles(const std::filesystem::path& thumbnail, const std::filesystem::path& atlas);

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
