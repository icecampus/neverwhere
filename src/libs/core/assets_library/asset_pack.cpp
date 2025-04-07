#include "asset_pack.h"

AssetPack::AssetPack(std::filesystem::path& packPath_, QObject* parent) :
    QAbstractListModel(parent),
    _packPath(packPath_)
{
    _uuid = QUuid::createUuid();
    _name = _packPath.filename().string().c_str();
    
}

QUuid AssetPack::uuid() const
{
    return _uuid;
}

QString AssetPack::getThumbnailUrl()
{
    QString url = QString("image://assetImages/") + _uuid.toString(QUuid::WithoutBraces);
    return url;
}

QString AssetPack::name() const
{
    return _name;
}


int AssetPack::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(m_assets.size());
}

QVariant AssetPack::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_assets.size()))
        return QVariant();

    if (role == ElementRole)
        return QVariant::fromValue(m_assets.at(index.row()).get());

    return QVariant();
}

QHash<int, QByteArray> AssetPack::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[ElementRole] = "element";
    return roles;
}

void AssetPack::addAsset(std::unique_ptr<Asset> asset)
{
    beginInsertRows(QModelIndex(), m_assets.size(), m_assets.size());
    m_assets.push_back(std::move(asset));
    endInsertRows();
}

QImage AssetPack::thumbnail()
{
    QImage result;

    auto thumbnailPath = _packPath / "thumbnail.png";
    if (fs::exists(thumbnailPath))
    {
        result.load(QString(thumbnailPath.c_str()));
    }

    return result;
}
