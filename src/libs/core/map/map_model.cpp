#include "map_model.h"
#include <QRandomGenerator>

#include "game_objects/tile_2d.h"
#include "game_objects/building.h"

MapModel::MapModel(QObject* parent):
    QAbstractListModel(parent) 
{

}

int MapModel::rowCount(const QModelIndex& parent) const 
{
    if (parent.isValid())
    {
        return 0;
    }
    return static_cast<int>(m_gameObjects.size());
}

QVariant MapModel::data(const QModelIndex& index, int role) const  
{
    if (!index.isValid() || index.row() >= static_cast<int>(m_gameObjects.size()))
    {
        return QVariant();
    }

    if (role == TypeRole)
    {
        return m_gameObjects[index.row()]->getType();
    }

    if (role == ElementRole)
    {
        return QVariant::fromValue(m_gameObjects[index.row()].get());
    }

    return QVariant();
}

QHash<int, QByteArray> MapModel::roleNames() const 
{
    QHash<int, QByteArray> roles;
    roles[ElementRole] = "element";
    roles[TypeRole] = "type";
    return roles;
}

void MapModel::addGameObject(std::unique_ptr<GameObject> gameObject) 
{
    beginInsertRows(QModelIndex(), m_gameObjects.size(), m_gameObjects.size());
    m_gameObjects.push_back(std::move(gameObject));
    endInsertRows();
}

GameObject* MapModel::getGameObject(int index) const 
{
    if (index >= 0 && index < static_cast<int>(m_gameObjects.size()))
        return m_gameObjects[index].get();
    return nullptr;
}

void MapModel::clear() 
{
    beginResetModel();
    m_gameObjects.clear();
    endResetModel();
}


void MapModel::populateMapModel()
{
    QRandomGenerator* rand = QRandomGenerator::global();

    std::vector<std::string>  imageSources{
        "a3771d55-8ca0-44aa-9d9f-5ab3e9cb300e", 
        "9813e80b-c6f7-43f9-9f11-f074009bb8f1", 
        "737179d4-0535-4141-ab2c-68758b71c141", 
        "a32aee74-1e74-45c4-a34c-de5e8847d1da", 
        "a476f78f-c329-4a78-be82-b7c5dbe49c6c"  
    };

    for (int i = 1; i <= 2000; ++i) {
        std::unique_ptr<Tile2D> gameObject = std::make_unique<Tile2D>();

        gameObject->setName(QString("Object %1").arg(i));

        int x = rand->generateDouble() * 200;
        int y = rand->generateDouble() * 200;
        math::ivec2 randomPosition(x, y);
        gameObject->setPosition(randomPosition);

        int assetIndex = rand->generateDouble() * 3;

        QString uuidStr = QString::fromStdString(imageSources[assetIndex]);

        QUuid uuid(uuidStr); 
        gameObject->setAssetUiid(uuid);

        addGameObject(std::move(gameObject));
    }
}