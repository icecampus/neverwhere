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

public:
    explicit Asset(QObject* parent);

    QUuid uuid() const;
    QString name() const;

protected:
    std::filesystem::path indexPath;
    QUuid m_uuid;           
    QString m_name;         
};