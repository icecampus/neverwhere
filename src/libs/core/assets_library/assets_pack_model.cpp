#include "assets_pack_model.h"

AssetsPackModel::AssetsPackModel(std::filesystem::path& packPath_, QObject* parent) :
    QAbstractListModel(parent),
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

Asset* AssetsPackModel::getCurrent()
{
    if (currentAssetIndex < _assets.size())
    {
        Asset* currentAsset = _assets[currentAssetIndex].get();
        return currentAsset;
    }

    return nullptr;
}

QString AssetsPackModel::name() const
{
    return _name;
}


int AssetsPackModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : static_cast<int>(_assets.size());
}

QVariant AssetsPackModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= static_cast<int>(_assets.size()))
        return QVariant();

    if (role == ElementRole)
        return QVariant::fromValue(_assets.at(index.row()).get());

    return QVariant();
}

QHash<int, QByteArray> AssetsPackModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[ElementRole] = "element";
    return roles;
}

void AssetsPackModel::addAsset(std::unique_ptr<Asset> asset)
{
    beginInsertRows(QModelIndex(), _assets.size(), _assets.size());
    _assets.push_back(std::move(asset));
    endInsertRows();
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

Q_INVOKABLE void AssetsPackModel::setCurrentByIndex(int index)
{
    if(currentAssetIndex != index)
    {
        currentAssetIndex = index;
        emit currentChanged();
    }
}
