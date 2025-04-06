#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QUuid>
#include "assets_library.h" 

class AssetImageProvider : public QQuickImageProvider 
{
public:
    AssetImageProvider(AssetsLibrary* library);

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    void loadAllImages();

    AssetsLibrary* _library = nullptr; 
    QMap<QUuid, QImage> _imageCache; 
};
