#pragma once
#include <QObject>
#include "assets_library_model.h"

class AssetsContext: public QObject
{
    Q_OBJECT
    Q_PROPERTY(AssetsLibraryModel* assetsLibrary READ getAssetsLibrary WRITE setAssetsLibrary NOTIFY assetsLibraryChanged)
    Q_PROPERTY(AssetsPackModel* assetPack READ getAssetsPack WRITE setAssetsPack NOTIFY assetsPackChanged)
    Q_PROPERTY(Asset* asset READ getAsset WRITE setAsset NOTIFY assetChanged)
public:
    AssetsContext(QObject* parent = nullptr);

    AssetsLibraryModel* getAssetsLibrary();
    void setAssetsLibrary(AssetsLibraryModel* assetsLibrary);

    AssetsPackModel* getAssetsPack();
    void setAssetsPack(AssetsPackModel* assetsPack);

    Asset* getAsset();
    void setAsset(Asset* asset);

signals:
    void assetsLibraryChanged();
    void assetsPackChanged();
    void assetChanged();

private:
    AssetsLibraryModel* assetsLibrary{nullptr};
    AssetsPackModel* assetsPack{nullptr};
    Asset* asset{nullptr};

};