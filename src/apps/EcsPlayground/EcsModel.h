#pragma once

#include <QAbstractListModel>
#include <entt/entt.hpp>
#include <vector>
#include <QString>

// --- Components ---
struct Position {
    float x;
    float y;
};

struct Velocity {
    float dx;
    float dy;
};

struct DisplayName {
    QString value;
};

// --- Model ---
class EcsModel : public QAbstractListModel {
    Q_OBJECT

public:
    enum Roles {
        NameRole = Qt::UserRole + 1,
        PositionXRole,
        PositionYRole
    };

    explicit EcsModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    // Simulation Methods
    Q_INVOKABLE void tick(float dt);
    Q_INVOKABLE void addRandomEntity();
    Q_INVOKABLE void clearEntities();

private:
    void updateCache();

    entt::registry m_registry;
    std::vector<entt::entity> m_entities; // Cache for QAbstractListModel stability
};
