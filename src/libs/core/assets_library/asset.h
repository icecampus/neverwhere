#pragma once
#include <QObject>
#include <QUuid>
#include <QJsonObject>
#include <QImage>
#include <QFileInfo>
#include <nlohmann/json.hpp>
#include <filesystem>

using json = nlohmann::json;
namespace fs = std::filesystem;

enum class AssetTypes: uint8_t
{
    image,
    slice
};

class Asset : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUuid uuid READ uuid CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)

public:
    explicit Asset(AssetTypes type, QObject* parent);

    QUuid uuid() const;
    QString name() const;

    virtual void load(const std::filesystem::path& indexPath,  const nlohmann::json& j);
    virtual QImage thumbnail() = 0;

    
    const AssetTypes type;
protected:
    std::filesystem::path indexPath;
    QUuid _uuid;           
    QString m_name;   
    
};