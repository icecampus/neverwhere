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
}

QImage SliceAsset::thumbnail()
{
    QImage result;
    if (fs::exists(thumbnailPath))
    {
        result.load(QString(thumbnailPath.c_str()));
    }

    return result;
}

QString SliceAsset::getUrlInternal() const
{
    QString url = QString("image://assetImages/") + _uuid.toString(QUuid::WithoutBraces);

    return url;

}
