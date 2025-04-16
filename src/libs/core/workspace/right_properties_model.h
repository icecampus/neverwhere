#pragma once
#include <QObject>

#include "typed_model.h"


namespace PropertiesContainerTypes
{
    Q_NAMESPACE;
    enum Type
    {
        Unknown,
        MapSettings,
        AssetSettings
    };
    Q_ENUM_NS(Type);
}


//PropertyContainer
class PropertyContainer: public QObject
{
    Q_OBJECT
    Q_PROPERTY(PropertiesContainerTypes::Type type READ getType CONSTANT)
    Q_PROPERTY(QString title READ getTitle CONSTANT)
public:
    PropertyContainer(PropertiesContainerTypes::Type type,  QObject* parent);

    PropertiesContainerTypes::Type getType() const;
    QString getTitle() const;
private:
    const PropertiesContainerTypes::Type type;
    QString title;

};

//MapSetting
class MapSetting: public PropertyContainer
{
    Q_OBJECT
public:
    MapSetting(QObject* parent);
};

//AssetSettings
class AssetSettings: public PropertyContainer
{
    Q_OBJECT
public:
    AssetSettings(QObject* parent);

};

//PropertyContainersModel
class PropertyContainersModel: public TypedModel<PropertyContainer>
{
    Q_OBJECT
public:
    PropertyContainersModel(QObject* parent = nullptr);


};