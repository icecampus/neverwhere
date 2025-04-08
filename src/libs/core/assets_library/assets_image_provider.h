#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QUuid>
#include "assets_library_model.h" 

class AssetImageProvider : public QQuickImageProvider 
{
public:
    AssetImageProvider(AssetsLibraryModel* library);
    void loadAllImages();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    

    AssetsLibraryModel* _library = nullptr; 
    QMap<QUuid, QImage> _imageCache; 
};
