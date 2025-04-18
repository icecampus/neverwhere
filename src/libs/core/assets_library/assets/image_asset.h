#pragma once
#include <nlohmann/json.hpp>
#include "assets_library/asset.h"
#include "topology/staggered_isometry.h"

class ImageAsset: public Asset
{
    Q_OBJECT
    Q_PROPERTY(float widthInCells READ getWidth WRITE setWidth NOTIFY widthChanged)
    Q_PROPERTY(QString imageFilename READ getImageFilename CONSTANT)

public:
    explicit ImageAsset(QObject* parent);

    //properties
    float getWidth() const;
    void setWidth(float widthInCells);

    QString getImageFilename() const;

    Q_INVOKABLE QSize getScreenSize(StaggeredIsometry* iso);
    Q_INVOKABLE void setScreenWidth(const float screenWidth, StaggeredIsometry* iso);

    //
    void load(const BaseData::AssetData& data) override;
    QImage thumbnail() override;

    void registerImages(RegistationHandle handle) override;
signals:
    void widthChanged();

protected:
    QString getUrlInternal() const override;

private:
    std::filesystem::path imagePath;
    QImage image;

};

