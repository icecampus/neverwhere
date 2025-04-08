#include "slice_asset.h"

SliceAsset::SliceAsset(QObject* parent):
    Asset(AssetTypes::slice, parent)
{

}

void SliceAsset::load(const std::filesystem::path& indexPath, const nlohmann::json& j)
{

}

QImage SliceAsset::thumbnail()
{
    return QImage();
}
