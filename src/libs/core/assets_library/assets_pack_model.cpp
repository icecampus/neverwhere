#include "assets_pack_model.h"

namespace fs = std::filesystem;

AssetsPackModel::AssetsPackModel(std::filesystem::path& packPath_, QObject* parent) :
    SimpleModel<Asset>(parent),
    _packPath(packPath_)
{
    _uuid = QUuid::createUuid();
    _name = _packPath.filename().string().c_str();
    
}

QUuid AssetsPackModel::uuid() const
{
    return _uuid;
}

QString AssetsPackModel::getThumbnailUrl()
{
    QString url = QString("image://assetImages/") + _uuid.toString(QUuid::WithoutBraces);
    return url;
}

QString AssetsPackModel::name() const
{
    return _name;
}

QImage AssetsPackModel::thumbnail()
{
    QImage result;

    auto thumbnailPath = _packPath / "thumbnail.png";
    if (fs::exists(thumbnailPath))
    {
        result.load(QString(thumbnailPath.c_str()));
    }

    return result;
}
