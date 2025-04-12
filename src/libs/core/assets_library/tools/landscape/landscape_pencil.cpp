#include "landscape_pencil.h"
#include "game_objects/landscape.h"

LandscapePencil::LandscapePencil(QObject* parent):
    Tool("LandscapePencil", "land_pencil", parent)
{

}

void LandscapePencil::click(QPoint screenPos, Asset* currentAsset, MapModel* mapModel, StaggeredIsometryView* iso)
{
    std::unique_ptr<Landscape> gameObject = std::make_unique<Landscape>();

    gameObject->setName(QString("Landscape"));

    math::ivec2 position = iso->screenToMap(math::vec2(screenPos.x(), screenPos.y()));
    gameObject->setPosition(position);
    gameObject->setAssetUiid(currentAsset->uuid());

    mapModel->addGameObject(std::move(gameObject));

}

