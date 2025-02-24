#pragma once
#include <QAbstractListModel>
#include <vector>
#include <memory>
#include "game_object.h"

class MapModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum GameObjectRoles {
        ElementRole = Qt::UserRole + 1
    };

    explicit MapModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}

    int rowCount(const QModelIndex &parent = QModelIndex()) const override {
        if (parent.isValid())
            return 0;
        return static_cast<int>(m_gameObjects.size());
    }

    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override {
        if (!index.isValid() || index.row() >= static_cast<int>(m_gameObjects.size()))
            return QVariant();

        if (role == ElementRole)
            return QVariant::fromValue(m_gameObjects[index.row()].get());

        return QVariant();
    }

    QHash<int, QByteArray> roleNames() const override {
        QHash<int, QByteArray> roles;
        roles[ElementRole] = "element";
        return roles;
    }

    void addGameObject(std::unique_ptr<GameObject> gameObject) {
        beginInsertRows(QModelIndex(), m_gameObjects.size(), m_gameObjects.size());
        m_gameObjects.push_back(std::move(gameObject));
        endInsertRows();
    }

    GameObject *getGameObject(int index) const {
        if (index >= 0 && index < static_cast<int>(m_gameObjects.size()))
            return m_gameObjects[index].get();
        return nullptr;
    }

    void clear() {
        beginResetModel();
        m_gameObjects.clear();
        endResetModel();
    }

private:
    std::vector<std::unique_ptr<GameObject>> m_gameObjects;
};