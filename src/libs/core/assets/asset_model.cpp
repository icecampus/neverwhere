#include "asset_model.h"

AssetModel::AssetModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int AssetModel::rowCount(const QModelIndex& parent) const 
{
    return parent.isValid() ? 0 : static_cast<int>(m_assets.size());
}

QVariant AssetModel::data(const QModelIndex& index, int role) const 
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_assets.size()))
        return QVariant();

    if (role == ElementRole)
        return QVariant::fromValue(m_assets.at(index.row()).get());

    return QVariant();
}

QHash<int, QByteArray> AssetModel::roleNames() const 
{
    QHash<int, QByteArray> roles;
    roles[ElementRole] = "element";
    return roles;
}

void AssetModel::addAsset(std::unique_ptr<Asset> asset)
{
    beginInsertRows(QModelIndex(), m_assets.size(), m_assets.size());
    m_assets.push_back(std::move(asset));
    endInsertRows();
}
