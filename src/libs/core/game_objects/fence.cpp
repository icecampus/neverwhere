#include "fence.h"
#include "base_data/lib.h"

Fence::Fence(QObject* parent):
    GameObject(GameObjectTypes::Fence, parent)
{
    data.fenceData = std::make_optional<BaseData::FenceData>();
}

void Fence::load(const BaseData::GameObject& data_)
{
    GameObject::load(data_);
}

int Fence::getKind() const
{
    return data.fenceData ? data.fenceData->kind : 0;
}

int Fence::getAxisX() const
{
    return data.fenceData ? data.fenceData->axisX : 0;
}

int Fence::getAxisY() const
{
    return data.fenceData ? data.fenceData->axisY : 0;
}

int Fence::getLength() const
{
    return data.fenceData ? data.fenceData->length : 1;
}

void Fence::setPiece(int kind, int axisX, int axisY, int length)
{
    if (!data.fenceData)
    {
        data.fenceData = BaseData::FenceData{};
    }
    data.fenceData->kind = kind;
    data.fenceData->axisX = axisX;
    data.fenceData->axisY = axisY;
    data.fenceData->length = length;
    emit fenceChanged();
}
