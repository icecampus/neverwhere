#include "asset_image_provider.h"

void AssetImageProvider::loadAllImages()
{
    for (int i = 0; i < m_model->rowCount(); ++i)
    {
        QModelIndex index = m_model->index(i);
        Asset* asset = m_model->data(index, AssetModel::ElementRole).value<Asset*>();
        if (asset) {
            QImage image = asset->image();
            if (!image.isNull()) {
                m_imageCache.insert(asset->uuid(), image);
            }
            else {
                qWarning() << "Failed to load image for asset with UUID" << asset->uuid();
            }
        }
    }
    qDebug() << "Loaded: " << m_imageCache.size() << " images from the model";
}