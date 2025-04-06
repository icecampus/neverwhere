#include "asset_image_provider.h"


AssetImageProvider::AssetImageProvider(AssetsLibrary* library_)
    : QQuickImageProvider(QQuickImageProvider::Image), _library(library_) 
{
    loadAllImages();
}


void AssetImageProvider::loadAllImages()
{
    //for (int i = 0; i < _library->rowCount(); ++i)
    //{
    //    QModelIndex index = m_model->index(i);
    //    Asset* asset = m_model->data(index, AssetsLibrary::ElementRole).value<Asset*>();
    //    
    //    if (asset) 
    //    {
    //        QImage image = asset->image();
    //        if (!image.isNull()) {
    //            _imageCache.insert(asset->uuid(), image);
    //        }
    //        else {
    //            qWarning() << "Failed to load image for asset with UUID" << asset->uuid();
    //        }
    //    }
    //}
}

QImage AssetImageProvider::requestImage(const QString& id, QSize* size, const QSize& requestedSize) 
{
    QUuid uuid(id);
    if (!_imageCache.contains(uuid)) {
        qWarning() << "Image for UUID: " << id << " can't find in cache";
        return QImage();
    }

    QImage image = _imageCache.value(uuid);
    if (size) *size = image.size();

    if (requestedSize.isValid()) {
        return image.scaled(requestedSize, Qt::KeepAspectRatio);
    }
    return image;
}
