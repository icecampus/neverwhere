#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QUuid>
#include "assets_library_model.h" 

class AssetImageProvider : public QQuickImageProvider 
{
public:
    struct ReuqestParams
    {
        QUuid uuid;
        std::optional<size_t> index;
    };

    AssetImageProvider(AssetsLibraryModel* library);
    void loadAllImages();

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    using ImageCache = std::map<QUuid, std::vector<QImage>>;

    ReuqestParams parse(const QString& request);

    AssetsLibraryModel* _library = nullptr; 
    ImageCache _imageCache; 
};
