#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QUuid>
#include "asset_model.h" 

class AssetImageProvider : public QQuickImageProvider {
public:
    AssetImageProvider(AssetModel* model)
        : QQuickImageProvider(QQuickImageProvider::Image), m_model(model) {
        loadAllImages(); 
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override {
        QUuid uuid(id);
        if (!m_imageCache.contains(uuid)) {
            qWarning() << "Image for UUID: " << id << " can't find in cache";
            return QImage(); 
        }

        QImage image = m_imageCache.value(uuid);
        if (size) *size = image.size();

        if (requestedSize.isValid()) {
            return image.scaled(requestedSize, Qt::KeepAspectRatio);
        }
        return image;
    }

private:
    void loadAllImages();

    AssetModel* m_model; 
    QMap<QUuid, QImage> m_imageCache; 
};
