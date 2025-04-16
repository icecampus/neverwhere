#pragma once
#include <QQuickImageProvider>
#include <QImage>
#include <QUuid>
#include "chapters_model.h"

class ChaptersImageProvider : public QQuickImageProvider 
{
public:
    ChaptersImageProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    
    }

    void load(ChaptersModel& model);

    QImage requestImage(const QString& id, QSize* size, const QSize& requestedSize) override;

private:
    QMap<QString, QImage> m_imageCache; 
};
