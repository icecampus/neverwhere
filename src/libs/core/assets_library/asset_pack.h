#pragma once
#include <QObject>
#include "asset.h"

class AssetPack: public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name CONSTANT)

public:
    enum AssetRoles
    {
        ElementRole = Qt::UserRole + 1
    };

    explicit AssetPack(QString name, QObject* parent = nullptr);

    QString name() const;

    //QAbstractListModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addAsset(std::unique_ptr<Asset> asset);


private:
    QString _name;
    std::vector<std::unique_ptr<Asset>> m_assets;
};
