#include "assets_image_provider.h"


AssetImageProvider::AssetImageProvider(AssetsLibraryModel* library_)
    : QQuickImageProvider(QQuickImageProvider::Image), _library(library_) 
{
    
}


void AssetImageProvider::loadAllImages()
{
    for (int packIndex = 0; packIndex < _library->size(); ++packIndex)
    {
        AssetsPackModel* pack = _library->element(packIndex);
        QImage packThumbnail = pack->thumbnail();
        _imageCache.insert(pack->uuid(), packThumbnail);

        for (int i = 0; i < pack->rowCount(); ++i)
        {
            QModelIndex index = pack->index(i);
            Asset* asset = pack->data(index, AssetsLibraryModel::ElementRole).value<Asset*>();

            if (asset)
            {
                QImage image = asset->thumbnail();
                if (!image.isNull()) 
                {
                    _imageCache.insert(asset->uuid(), image);
                }
                else 
                {
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
