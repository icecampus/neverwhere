#pragma once
#include <QAbstractListModel>
#include <vector>
#include <memory>
#include "game_object.h"
#include "simple_model.h"

class LayerModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum GameObjectRoles 
    {
        ElementRole = Qt::UserRole + 1,
        TypeRole
    };

    explicit LayerModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addGameObject(std::unique_ptr<GameObject> gameObject);
    GameObject *getGameObject(int index) const;

    void clear();

    

private:
    std::vector<std::unique_ptr<GameObject>> m_gameObjects;
};

//MapModel
class MapModel: public SimpleModel<LayerModel>
{
    Q_OBJECT
public:
    explicit MapModel(QObject* parent = nullptr);

    LayerModel* layer(LayerTypes::Type type);
    void addGameObject(LayerTypes::Type layerType, std::unique_ptr<GameObject> gameObject);

    Q_INVOKABLE void populateMapModel();

};