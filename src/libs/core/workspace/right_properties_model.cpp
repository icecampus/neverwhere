#include "right_properties_model.h"
#include <magic_enum/magic_enum.hpp>

PropertyContainer::PropertyContainer(PropertiesContainerTypes::Type type_, QObject* parent):
    QObject(parent),
    type(type_)
{

}

PropertiesContainerTypes::Type PropertyContainer::getType() const
{
    return type;
}

QString PropertyContainer::getTitle() const
{
    return QString::fromStdString(std::string(magic_enum::enum_name(type)));
}

//
MapSetting::MapSetting(QObject* parent):
    PropertyContainer(PropertiesContainerTypes::MapSettings, parent)
{

}

//
AssetSettings::AssetSettings(QObject* parent):
    PropertyContainer(PropertiesContainerTypes::AssetSettings, parent)
{

}

//PropertyContainersModel
PropertyContainersModel::PropertyContainersModel(QObject* parent):
    TypedModel<PropertyContainer>(parent)
{
    addElement<MapSetting>(this);
    addElement<AssetSettings>(this);
}
