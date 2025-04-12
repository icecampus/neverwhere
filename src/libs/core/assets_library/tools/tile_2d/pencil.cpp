#include "pencil.h"
#include "game_objects/tile_2d.h"

Pencil::Pencil(QObject* parent):
    Tool("Pencil", "pencil", parent)
{

}

void Pencil::click(QPoint screenPos, Asset* currentAsset, MapModel* mapModel, StaggeredIsometryView* iso)
{
    std::unique_ptr<Tile2D> gameObject = std::make_unique<Tile2D>();

    gameObject->setName(QString("Object"));

    math::ivec2 position = iso->screenToMap(math::vec2(screenPos.x(), screenPos.y()));
    gameObject->setPosition(position);
    gameObject->setAssetUiid(currentAsset->uuid());

    mapModel->addGameObject(std::move(gameObject));

}

