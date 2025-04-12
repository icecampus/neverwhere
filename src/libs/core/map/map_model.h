#pragma once
#include <QAbstractListModel>
#include <vector>
#include <memory>
#include "game_object.h"

class MapModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum GameObjectRoles 
    {
        ElementRole = Qt::UserRole + 1,
        TypeRole
    };

    explicit MapModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addGameObject(std::unique_ptr<GameObject> gameObject);
    GameObject *getGameObject(int index) const;

    void clear();

    Q_INVOKABLE void populateMapModel();

private:
    std::vector<std::unique_ptr<GameObject>> m_gameObjects;
};

