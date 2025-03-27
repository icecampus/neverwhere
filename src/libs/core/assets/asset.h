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

class Asset : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QUuid uuid READ uuid CONSTANT)
    Q_PROPERTY(QString name READ name CONSTANT)
    Q_PROPERTY(int width READ width CONSTANT)
    Q_PROPERTY(QString imageFilename READ imageFilename CONSTANT)
    Q_PROPERTY(QString url READ getUrl CONSTANT)

public:
    explicit Asset(const std::filesystem::path& indexPath, const json& j, const std::string& assetPath, QObject* parent = nullptr);

    QUuid uuid() const { return m_uuid; }
    QString name() const { return m_name; }
    int width() const { return m_width; }
    QString imageFilename() const { return m_imageFilename; }
    QImage image() const { return m_image; }
    QString getUrl() const;

    
    json toJson() const {
        json graphics = {
            {"width", m_width},
            {"imageFilename", m_imageFilename.toStdString()}
        };

        json j = {
            {"uuid", m_uuid.toString(QUuid::WithoutBraces).toStdString()},
            {"name", m_name.toStdString()},
            {"graphics", graphics}
        };

        return j;
    }

private:
    std::filesystem::path indexPath;
    QUuid m_uuid;           
    QString m_name;         
    int m_width;            
    QString m_imageFilename;
    QImage m_image;         
};