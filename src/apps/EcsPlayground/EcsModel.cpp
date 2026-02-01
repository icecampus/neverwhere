#include "EcsModel.h"
#include <random>

EcsModel::EcsModel(QObject* parent) : QAbstractListModel(parent) {
    // Initial population
    addRandomEntity();
    addRandomEntity();
    addRandomEntity();
}

int EcsModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_entities.size());
}

QVariant EcsModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_entities.size())
        return QVariant();

    const auto entity = m_entities[index.row()];

    // Check if entity is still valid
    if (!m_registry.valid(entity)) return QVariant();

    switch (role) {
        case NameRole:
            if (auto* name = m_registry.try_get<DisplayName>(entity)) {
                return name->value;
            }
            return QString("Unknown");

        case PositionXRole:
            if (auto* pos = m_registry.try_get<Position>(entity)) {
                return pos->x;
            }
            return 0.0f;

        case PositionYRole:
            if (auto* pos = m_registry.try_get<Position>(entity)) {
                return pos->y;
            }
            return 0.0f;
    }

    return QVariant();
}

QHash<int, QByteArray> EcsModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[PositionXRole] = "posX";
    roles[PositionYRole] = "posY";
    return roles;
}

void EcsModel::tick(float dt) {
    // System: Movement
    auto view = m_registry.view<Position, const Velocity>();
    
    // We assume data changes, so we might need to signal updates.
    // In a high-perf scenario, we would only signal changed rows.
    // Here we assume everything moves.
    
    view.each([dt](Position& pos, const Velocity& vel) {
        pos.x += vel.dx * dt;
        pos.y += vel.dy * dt;

        // Bounce from logical bounds (0..800, 0..600)
        if (pos.x < 0 || pos.x > 800) pos.x = 0; // Simple reset for demo
        if (pos.y < 0 || pos.y > 600) pos.y = 0;
    });

    // Notify QML that data changed
    if (!m_entities.empty()) {
        emit dataChanged(index(0), index(m_entities.size() - 1), {PositionXRole, PositionYRole});
    }
}

void EcsModel::addRandomEntity() {
    beginInsertRows(QModelIndex(), m_entities.size(), m_entities.size());

    auto entity = m_registry.create();
    
    // Random gen
    static std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<float> distPos(50.0f, 500.0f);
    std::uniform_real_distribution<float> distVel(-50.0f, 50.0f);
    
    m_registry.emplace<Position>(entity, distPos(gen), distPos(gen));
    m_registry.emplace<Velocity>(entity, distVel(gen), distVel(gen));
    m_registry.emplace<DisplayName>(entity, QString("Entity #%1").arg(static_cast<uint32_t>(entity)));

    m_entities.push_back(entity);

    endInsertRows();
}

void EcsModel::clearEntities() {
    beginResetModel();
    m_registry.clear();
    m_entities.clear();
    endResetModel();
}

void EcsModel::updateCache() {
    // Used if we need to sync m_entities with registry explicitly
    // Not used in this simple example where we push_back directly
}
