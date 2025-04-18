#pragma once
#include <QObject>
#include "asset.h"
#include "simple_model.h"

class AssetsPackModel: public SimpleModel<Asset>
{
    Q_OBJECT
    Q_PROPERTY(QUuid uuid READ uuid CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(QString thumbnailUrl READ getThumbnailUrl CONSTANT)
    
public:
    explicit AssetsPackModel(const std::filesystem::path& packPath, QObject* parent = nullptr);

    //property
    QString name() const;
    QUuid uuid() const;
    QString getThumbnailUrl();

    QImage thumbnail();
signals:

private:

    std::filesystem::path _packPath;
    QUuid _uuid;
    QString _name;
    QImage _thumbnail;
};
