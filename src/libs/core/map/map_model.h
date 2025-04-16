#pragma once
#include <QAbstractListModel>
#include <vector>
#include <memory>
#include "game_object.h"
#include "simple_model.h"
#include "base_data/lib.h"

//LayerModel
class LayerModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum GameObjectRoles 
    {
        ElementRole = Qt::UserRole + 1,
        TypeRole
    };

    using GameObjectHandler = std::function<void(GameObject& gameObject)>;

    explicit LayerModel(QObject *parent);

    void load(const BaseData::Layer& layer);
    void save(BaseData::Layer& layer);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addGameObject(std::unique_ptr<GameObject> gameObject);
    GameObject *getGameObject(int index) const;
    std::vector<GameObject*> getObjectsAt(const math::ivec2& position);
    void iterate(GameObjectHandler handler);

    void clear();

private slots:
    void onGameObjectPositionChanged();
    
private:
    std::vector<std::unique_ptr<GameObject>> _gameObjects;
    std::unordered_map<math::ivec2, std::vector<GameObject*>> _positionMap;
    std::unordered_map<GameObject*, math::ivec2> _objectOldPositions;
};

//MapModel
class MapModel: public SimpleModel<LayerModel>
{
    Q_OBJECT
public:
    explicit MapModel(QObject* parent = nullptr);

    LayerModel* layer(LayerTypes::Type type);
    void addGameObject(LayerTypes::Type layerType, std::unique_ptr<GameObject> gameObject);

    Q_INVOKABLE void load(QString mapPath);
    Q_INVOKABLE void save(QString mapPath);

};