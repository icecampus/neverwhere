#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QByteArray>
#include <vector>
#include <memory>
#include "asset.h"


class AssetModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit AssetModel(QObject* parent = nullptr)
        : QAbstractListModel(parent)
    {
    }

    enum AssetRoles {
        ElementRole = Qt::UserRole + 1
    };

    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_assets.size());
    }

    QVariant data(const QModelIndex& index, int role) const override
    {
        if (!index.isValid() || index.row() >= static_cast<int>(m_assets.size()))
            return QVariant();

        if (role == ElementRole)
            return QVariant::fromValue(m_assets.at(index.row()).get()); 

        return QVariant();
    }


    QHash<int, QByteArray> roleNames() const override
    {
        QHash<int, QByteArray> roles;
        roles[ElementRole] = "element"; 
        return roles;
    }

    void addAsset(std::unique_ptr<Asset> asset)
    {
        beginInsertRows(QModelIndex(), m_assets.size(), m_assets.size());
        m_assets.push_back(std::move(asset)); 
        endInsertRows();
    }

private:
    std::vector<std::unique_ptr<Asset>> m_assets;
};
