#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QUuid>

class ChaptersImageProvider : public QQuickImageProvider 
{
public:
    ChaptersImageProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    
    }

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    QMap<QString, QImage> m_imageCache; 
};
