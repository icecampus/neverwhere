#pragma once
#include <nlohmann/json.hpp>
#include "assets_library/asset.h"

class ImageAsset: public Asset
{
    Q_OBJECT
    Q_PROPERTY(int width READ width CONSTANT)
    Q_PROPERTY(QString imageFilename READ imageFilename CONSTANT)

public:
    explicit ImageAsset(QObject* parent);

    //properties
    int width() const;
    QString imageFilename() const;

    //
    void load(const std::filesystem::path& indexPath,  const nlohmann::json& j) override;
    QImage thumbnail() override;

    nlohmann::json save();

protected:
    QString getUrlInternal() const override;

private:
    int m_width;
    QString m_imageFilename;
    fs::path imagePath;
};

