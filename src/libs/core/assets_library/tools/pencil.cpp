#include "pencil.h"

Pencil::Pencil(QObject* parent):
    Tool("Pencil", "pencil", parent)
{

}

void Pencil::click(QPoint screenPos, Asset* currentAsset, MapModel* mapModel, StaggeredIsometryView* iso)
{
    std::unique_ptr<GameObject> gameObject = std::make_unique<GameObject>();

    gameObject->setName(QString("Object"));

    math::ivec2 position = iso->screenToMap(math::vec2(screenPos.x(), screenPos.y()));
    gameObject->setPosition(position);
    gameObject->setAssetUiid(currentAsset->getThumbnailUrl());

    mapModel->addGameObject(std::move(gameObject));

}

