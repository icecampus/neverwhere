#pragma once

#include <QAbstractListModel>
#include <entt/entt.hpp>
#include <vector>
#include <QString>

class GameModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        EntityIdRole = Qt::UserRole + 1,
        DisplayNameRole,
        PositionXRole,
        PositionYRole
    };

    explicit GameModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // --- Reflection API for QML ---
    Q_INVOKABLE QVariantList getInspectorData(int row);

    // Update a property value via RTTR
    Q_INVOKABLE bool setProperty(int row, const QString& compName, const QString& propName, const QVariant& value);

    // --- Simulation / Setup ---
    Q_INVOKABLE void addRandomEntity();

    // Self-test
    void runTestScenario();

private:
    entt::registry m_registry;
    std::vector<entt::entity> m_entities;

    void createSampleData();
};
