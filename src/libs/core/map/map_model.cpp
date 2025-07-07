#include "map_model.h"
#include <QRandomGenerator>

#include "game_objects/tile_2d.h"
#include "game_objects/resource.h"
#include "game_objects/building.h"
#include "game_objects/landscape.h"
#include <magic_enum/magic_enum.hpp>

namespace fs = std::filesystem;

LayerModel::LayerModel(QObject* parent):
    QAbstractListModel(parent) 
{

}

void LayerModel::load(const BaseData::Layer& layer)
{
    for (const BaseData::GameObject& gameObject : layer)
    {
        if (gameObject.tile2dData)
        {
            auto go = std::make_unique<Tile2D>(this);
            go->load(gameObject);

            addGameObject(std::move(go));

        }
        if (gameObject.resourceData)
        {
            auto go = std::make_unique<Resource>(this);
            go->load(gameObject);

            addGameObject(std::move(go));

        }
        if (gameObject.landscapeData)
        {
            auto go = std::make_unique<Landscape>(this);
            go->load(gameObject);

            addGameObject(std::move(go));
            
        }
        if (gameObject.buildingData)
        {
            auto go = std::make_unique<Building>(this);
            go->load(gameObject);

            addGameObject(std::move(go));
        }   
        
    }
}

void LayerModel::save(BaseData::Layer& layer)
{
    for (auto& go: _gameObjects)
    {
        layer.push_back(go->getData());
    }
}

int LayerModel::rowCount(const QModelIndex& parent) const 
{
    if (parent.isValid())
    {
        return 0;
    }
    return static_cast<int>(_gameObjects.size());
}

QVariant LayerModel::data(const QModelIndex& index, int role) const  
{
    if (!index.isValid() || index.row() >= static_cast<int>(_gameObjects.size()))
    {
        return QVariant();
    }

    if (role == TypeRole)
    {
        GameObjectTypes::Type type = getGameObject(index.row())->getType();
        return type;
    }

    if (role == ElementRole)
    {
        return QVariant::fromValue(getGameObject(index.row()));
    }

    return QVariant();
}

QHash<int, QByteArray> LayerModel::roleNames() const 
{
    QHash<int, QByteArray> roles;
    roles[ElementRole] = "element";
    roles[TypeRole] = "type";
    return roles;
}

void LayerModel::addGameObject(std::unique_ptr<GameObject> gameObject) 
{
    GameObject* obj = gameObject.get();

    beginInsertRows(QModelIndex(), _gameObjects.size(), _gameObjects.size());
    _gameObjects.push_back(std::move(gameObject));

    connect(obj, &GameObject::positionChanged, this, &LayerModel::onGameObjectPositionChanged);

    // Обновляем структуры данных
    math::ivec2 pos = obj->getPosition();
    _objectOldPositions[obj] = pos;
    _positionMap[pos].push_back(obj);

    endInsertRows();
}

GameObject* LayerModel::getGameObject(int index) const 
{
    if (index >= 0 && index < static_cast<int>(_gameObjects.size()))
    {
        return _gameObjects[index].get();
    }

    return nullptr;
}

std::vector<GameObject*> LayerModel::getObjectsAt(const math::ivec2& position)
{
    auto it = _positionMap.find(position);
    if (it != _positionMap.end()) 
    {
        return it->second;
    }
    return {};
}

void LayerModel::iterate(GameObjectHandler handler)
{
    for (auto& gameObject: _gameObjects)
    {
        handler(*gameObject.get());
    }
}

void LayerModel::clear() 
{
    beginResetModel();
    _gameObjects.clear();
    _positionMap.clear();
    _objectOldPositions.clear();
    endResetModel();
}



void LayerModel::onGameObjectPositionChanged()
{
    GameObject* obj = qobject_cast<GameObject*>(sender());
    if (!obj || _objectOldPositions.find(obj) == _objectOldPositions.end()) return;

    math::ivec2 oldPos = _objectOldPositions[obj];
    math::ivec2 newPos = obj->getPosition();

    // Удаляем из старой позиции
    auto& oldList = _positionMap[oldPos];
    oldList.erase(std::remove(oldList.begin(), oldList.end(), obj), oldList.end());
    if (oldList.empty()) _positionMap.erase(oldPos);

    // Добавляем в новую позицию
    _positionMap[newPos].push_back(obj);
    _objectOldPositions[obj] = newPos;
}

//MapModel
MapModel::MapModel(QObject* parent):
    SimpleModel<LayerModel>(parent)
{
    for (const LayerTypes::Type layer : magic_enum::enum_values<LayerTypes::Type>())
    {
        addElement<LayerModel>(this);        
    }
}


LayerModel* MapModel::layer(LayerTypes::Type type)
{
    return element(magic_enum::enum_index<LayerTypes::Type>(type).value());
}

void MapModel::addGameObject(LayerTypes::Type layerType, std::unique_ptr<GameObject> gameObject)
{
    layer(layerType)->addGameObject(std::move(gameObject));
}

void MapModel::load(QString mapPath)
{
    fs::path path = mapPath.toStdString();

    if (fs::exists(path))
    {
        BaseData::Map baseMap = BaseData::Map::load(mapPath.toStdString());

        for (const LayerTypes::Type layerType : magic_enum::enum_values<LayerTypes::Type>())
        {
            size_t layerIndex = magic_enum::enum_index(layerType).value();
            element(layerIndex)->load(baseMap.at(layerType));
        }
    }


    //QRandomGenerator* rand = QRandomGenerator::global();

    //std::vector<std::string>  imageSources{
    //    "a3771d55-8ca0-44aa-9d9f-5ab3e9cb300e",
    //    "9813e80b-c6f7-43f9-9f11-f074009bb8f1",
    //    "737179d4-0535-4141-ab2c-68758b71c141",
    //    "a32aee74-1e74-45c4-a34c-de5e8847d1da",
    //    "a476f78f-c329-4a78-be82-b7c5dbe49c6c"
    //};

    //for (int i = 1; i <= 200; ++i) 
    //{
    //    std::unique_ptr<Tile2D> gameObject = std::make_unique<Tile2D>();

    //    gameObject->setName(QString("Object %1").arg(i));

    //    int x = rand->generateDouble() * 200;
    //    int y = rand->generateDouble() * 200;
    //    math::ivec2 randomPosition(x, y);
    //    gameObject->setPosition(randomPosition);

    //    int assetIndex = rand->generateDouble() * 3;

    //    QString uuidStr = QString::fromStdString(imageSources[assetIndex]);

    //    QUuid uuid(uuidStr);
    //    gameObject->setAssetUiid(uuid);

    //    addGameObject(LayerTypes::GameplayInteractive, std::move(gameObject));
    //}
}

void MapModel::save(QString mapPath)
{
    BaseData::Map baseMap;
    for (const LayerTypes::Type layerType : magic_enum::enum_values<LayerTypes::Type>())
    {
        size_t layerIndex = magic_enum::enum_index(layerType).value();
        element(layerIndex)->save(baseMap[layerType]);
    }

    BaseData::Map::save(baseMap, mapPath.toStdString());
}
