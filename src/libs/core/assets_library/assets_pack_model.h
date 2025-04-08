#pragma once
#include <QObject>
#include "asset.h"

class AssetsPackModel: public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QUuid uuid READ uuid CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString thumbnailUrl READ getThumbnailUrl CONSTANT)
    
public:
    enum AssetRoles
    {
        ElementRole = Qt::UserRole + 1
    };

    explicit AssetsPackModel(std::filesystem::path& packPath, QObject* parent = nullptr);

    //property
    QString name() const;
    QUuid uuid() const;
    QString getThumbnailUrl();
    Asset* getCurrent();

    //QAbstractListModel
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    //
    void addAsset(std::unique_ptr<Asset> asset);
    QImage thumbnail();

signals:
    void currentChanged();

private:

    std::filesystem::path _packPath;
    QUuid _uuid;
    QString _name;
    QImage _thumbnail;

    std::vector<std::unique_ptr<Asset>> _assets;
};
