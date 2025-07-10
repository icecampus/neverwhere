#include "assets_image_provider.h"
#include <QUrlQuery>


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
        _imageCache[pack->uuid()].push_back(packThumbnail);

        for (int i = 0; i < pack->rowCount(); ++i)
        {
            QModelIndex index = pack->index(i);
            Asset* asset = pack->data(index, AssetsLibraryModel::ElementRole).value<Asset*>();

            if (asset)
            {
                asset->registerImages([&](int index, const QImage& image)
                {
                    if (!image.isNull())
                    {
                        _imageCache[asset->uuid()].push_back(image);
                    }
                    else
                    {
                        //spdlog::error( "Failed to load image for asset with UUID {} ", asset->uuid().toString().toStdString());
                    }

                });
            }
        }

    }
}

// use HTTP like format image://provider/mainId?color=green&size=200x300
// alternative use json "image://provider/{\"color\":\"green\", \"size\":\"200x300\"} 
// or image://provider/{"gradient":["#FF0000", "#00FF00"], "angle":45}
QImage AssetImageProvider::requestImage(const QString& request, QSize* size, const QSize& requestedSize) 
{
    ReuqestParams params = parse(request);
    if (_imageCache.count(params.uuid)) 
    {
        std::vector<QImage>& assetImages = _imageCache.at(params.uuid);

        QImage image;
        image = assetImages[params.index.value_or(0)];
        
        if (size)
        { 
            *size = image.size();
        }

        if (requestedSize.isValid()) 
        {
            return image.scaled(requestedSize, Qt::KeepAspectRatio);
        }

        return image;
    }

    qWarning() << "Image for UUID: " << request << " can't find in cache";
    return QImage();
}

AssetImageProvider::ReuqestParams AssetImageProvider::parse(const QString& request)
{
    ReuqestParams result;

    QStringList parts = request.split('?');
    result.uuid = QUuid(parts[0]);

    QUrlQuery query(parts.size() > 1 ? parts[1] : "");

    if (query.hasQueryItem("index"))
    {
        result.index = query.queryItemValue("index").toUInt();
    }

    return result;

}
