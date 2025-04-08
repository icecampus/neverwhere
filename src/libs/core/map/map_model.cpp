#include "map_model.h"
#include <QRandomGenerator>

void MapModel::populateMapModel()
{
    QRandomGenerator* rand = QRandomGenerator::global();

    std::vector<std::string>  imageSources{
        "image://assetImages/a3771d55-8ca0-44aa-9d9f-5ab3e9cb300e", 
        "image://assetImages/9813e80b-c6f7-43f9-9f11-f074009bb8f1", 
        "image://assetImages/737179d4-0535-4141-ab2c-68758b71c141", 
        "image://assetImages/a32aee74-1e74-45c4-a34c-de5e8847d1da", 
        "image://assetImages/a476f78f-c329-4a78-be82-b7c5dbe49c6c"  
    };

    for (int i = 1; i <= 2000; ++i) {
        std::unique_ptr<GameObject> gameObject = std::make_unique<GameObject>();

        gameObject->setName(QString("Object %1").arg(i));

        int x = rand->generateDouble() * 200;
        int y = rand->generateDouble() * 200;
        math::ivec2 randomPosition(x, y);
        gameObject->setPosition(randomPosition);

        int assetIndex = rand->generateDouble() * 3;
        gameObject->setAssetUiid(QString::fromStdString(imageSources[assetIndex]));

        addGameObject(std::move(gameObject));
    }
}