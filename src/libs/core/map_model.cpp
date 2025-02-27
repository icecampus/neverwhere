#include "map_model.h"
#include <QRandomGenerator>

void MapModel::populateMapModel()
{
    QRandomGenerator* rand = QRandomGenerator::global();

    for (int i = 1; i <= 20; ++i) {
        std::unique_ptr<GameObject> gameObject = std::make_unique<GameObject>();

        gameObject->setName(QString("Object %1").arg(i));

        int x = rand->generateDouble() * 20;
        int y = rand->generateDouble() * 20;
        math::ivec2 randomPosition(x, y);
        gameObject->setPosition(randomPosition);

        addGameObject(std::move(gameObject));
    }
}