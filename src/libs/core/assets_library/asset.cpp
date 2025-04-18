#include "asset.h"
#include <magic_enum/magic_enum.hpp>


using json = nlohmann::json;
namespace fs = std::filesystem;

Asset::Asset(AssetTypes::Type type_, QObject* parent):
    QObject(parent),
    type(type_)
{
}

QUuid Asset::uuid() const 
{ 
    return base::boostUuidToQUuid(data.uuid); 
}

QString Asset::name() const 
{ 
    return data.name.c_str(); 
}

QString Asset::getThumbnailUrl() const
{
    return getUrlInternal();
}

LayerTypes::Type Asset::getLayerType() const
{
    return data.layerType;
}

math::vec2 Asset::getPivot() const
{
    return data.pivot;
}

void Asset::setPivot(const math::vec2& pivot)
{
    //spdlog::info("pivot: {}", pivot_);
    
    if (data.pivot != pivot)
    {
        data.pivot = pivot;
        emit pivotChanged();
    }
}

bool Asset::getEditMode() const
{
    return editMode;
}

void Asset::setEditMode(bool state)
{
    if (editMode != state)
    {
        editMode = state;
        emit editModeChanged();
    }
}

void Asset::load(const BaseData::AssetData& data_)
{
    data = data_;
}

const BaseData::AssetData& Asset::getData()
{
    return data;
}
