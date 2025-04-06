#include "asset_image_provider.h"


AssetImageProvider::AssetImageProvider(AssetsLibrary* library_)
    : QQuickImageProvider(QQuickImageProvider::Image), _library(library_) 
{
    
}


void AssetImageProvider::loadAllImages()
{
    for (int packIndex = 0; packIndex < _library->rowCount(); ++packIndex)
    {
        AssetPack* pack = _library->element(packIndex);

        for (int i = 0; i < pack->rowCount(); ++i)
        {
            QModelIndex index = pack->index(i);
            Asset* asset = pack->data(index, AssetsLibrary::ElementRole).value<Asset*>();

            if (asset)
            {
                QImage image = asset->thumbnail();
                if (!image.isNull()) {
                    _imageCache.insert(asset->uuid(), image);
                }
                else {
                    qWarning() << "Failed to load image for asset with UUID" << asset->uuid();
                }
            }
        }

    }
}

QImage AssetImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) 
{
    QUuid uuid(id);
    if (!_imageCache.contains(uuid)) {
        qWarning() << "Image for UUID: " << id << " can't find in cache";
        return QImage();
    }

    QImage image = _imageCache.value(uuid);
    if (size)
    { 
        *size = image.size();
    }

    if (requestedSize.isValid()) {
        return image.scaled(requestedSize, Qt::KeepAspectRatio);
    }
    return image;
}
