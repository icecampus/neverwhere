#pragma once
#include "assets_library/asset.h"
#include <array>
#include "topology/staggered_isometry.h"

class SliceAsset: public Asset
{
    Q_OBJECT

    
public:
    explicit SliceAsset(QObject* parent);

    //properties
    void load(const std::filesystem::path& indexPath,  const nlohmann::json& j) override;
    QImage thumbnail() override;

    //
    Q_INVOKABLE QSize getSize(StaggeredIsometry* iso);

protected:
    QString getUrlInternal() const override;

private:
    static std::vector<QImage> splitImageIntoTiles(const QImage& source, int cols, int rows);

    std::string thumbnailFilename;
    std::filesystem::path thumbnailPath;
    QImage thumbnailImage;

    std::string atlasFilename;
    std::filesystem::path atlasPath;
    QImage atlasImage;

    std::vector<QImage> tiles;
};
