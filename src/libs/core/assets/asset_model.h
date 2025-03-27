#pragma once
#include <QAbstractListModel>
#include <QHash>
#include <QByteArray>
#include <vector>
#include <memory>
#include "asset.h"


class AssetModel: public QAbstractListModel
{
    Q_OBJECT

public:
    enum AssetRoles 
    {
        ElementRole = Qt::UserRole + 1
    };

    explicit AssetModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addAsset(std::unique_ptr<Asset> asset);

private:
    std::vector<std::unique_ptr<Asset>> m_assets;
};
