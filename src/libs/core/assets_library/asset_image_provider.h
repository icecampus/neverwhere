#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QUuid>
#include "assets_library.h" 

class AssetImageProvider : public QQuickImageProvider 
{
public:
    AssetImageProvider(AssetsLibrary* library);
    void loadAllImages();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    

    AssetsLibrary* _library = nullptr; 
    QMap<QUuid, QImage> _imageCache; 
};
