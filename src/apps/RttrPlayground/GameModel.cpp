#include "GameModel.h"
#include "Components.h"
#include <rttr/type>
#include <rttr/property.h>
#include <random>
#include <tuple>
#include <spdlog/spdlog.h>

// Helper to iterate over types
template<typename Tuple, typename Func>
void for_each_component_type(Func&& f) {
    std::apply([&f](auto&&... args) {
        (f(args), ...);
    }, Tuple{});
}

using ComponentList = std::tuple<TransformComponent, NameComponent, HealthComponent>;

GameModel::GameModel(QObject* parent) : QAbstractListModel(parent) {
    createSampleData();
}

int GameModel::rowCount(const QModelIndex& parent) const {
    if (parent.isValid()) return 0;
    return static_cast<int>(m_entities.size());
}

QVariant GameModel::data(const QModelIndex& index, int role) const {
    if (!index.isValid() || index.row() >= m_entities.size())
        return QVariant();

    const auto entity = m_entities[index.row()];
    if (!m_registry.valid(entity)) return QVariant();

    if (role == EntityIdRole) {
        return static_cast<quint32>(entity);
    } else if (role == DisplayNameRole) {
        if (auto* name = m_registry.try_get<NameComponent>(entity)) {
            return QString::fromStdString(name->name);
        }
        return QString("Entity %1").arg(static_cast<uint32_t>(entity));
    } else if (role == PositionXRole) {
        if (auto* pos = m_registry.try_get<TransformComponent>(entity)) {
            return pos->x;
        }
        return 0.0f;
    } else if (role == PositionYRole) {
        if (auto* pos = m_registry.try_get<TransformComponent>(entity)) {
            return pos->y;
        }
        return 0.0f;
    }

    return QVariant();
}

QHash<int, QByteArray> GameModel::roleNames() const {
    QHash<int, QByteArray> roles;
    roles[EntityIdRole] = "entityId";
    roles[DisplayNameRole] = "display";
    roles[PositionXRole] = "posX";
    roles[PositionYRole] = "posY";
    return roles;
}

QVariantList GameModel::getInspectorData(int row) {
    QVariantList componentsData;
    if (row < 0 || row >= m_entities.size()) {
        spdlog::warn("getInspectorData: Invalid row index {}", row);
        return componentsData;
    }

    auto entity = m_entities[row];
    if (!m_registry.valid(entity)) {
        spdlog::warn("getInspectorData: Invalid entity at row {}", row);
        return componentsData;
    }

    spdlog::info("Inspecting Entity ID: {}", static_cast<uint32_t>(entity));

    for_each_component_type<ComponentList>([&](auto t) {
        using ComponentType = std::decay_t<decltype(t)>;
        
        if (m_registry.all_of<ComponentType>(entity)) {
            ComponentType& comp = m_registry.get<ComponentType>(entity);
            rttr::instance obj(comp);
            rttr::type type = rttr::type::get<ComponentType>();
            
            spdlog::debug("  Found Component: {}", type.get_name().to_string());

            QVariantMap compMap;
            compMap["name"] = QString::fromStdString(type.get_name().to_string());
            
            QVariantList propsList;
            for (auto& prop : type.get_properties()) {
                QVariantMap propMap;
                propMap["name"] = QString::fromStdString(prop.get_name().to_string());
                propMap["typeName"] = QString::fromStdString(prop.get_type().get_name().to_string());
                
                rttr::variant val = prop.get_value(obj);
                
                // Manual conversion for simple types to QVariant
                if (val.is_type<float>()) {
                    float v = val.get_value<float>();
                    propMap["value"] = v;
                    spdlog::trace("    Prop: {} = {} (float)", prop.get_name().to_string(), v);
                }
                else if (val.is_type<double>()) {
                    double v = val.get_value<double>();
                    propMap["value"] = v;
                    spdlog::trace("    Prop: {} = {} (double)", prop.get_name().to_string(), v);
                }
                else if (val.is_type<int>()) {
                    int v = val.get_value<int>();
                    propMap["value"] = v;
                    spdlog::trace("    Prop: {} = {} (int)", prop.get_name().to_string(), v);
                }
                else if (val.is_type<bool>()) {
                    bool v = val.get_value<bool>();
                    propMap["value"] = v;
                    spdlog::trace("    Prop: {} = {} (bool)", prop.get_name().to_string(), v);
                }
                else if (val.is_type<std::string>()) {
                    std::string v = val.get_value<std::string>();
                    propMap["value"] = QString::fromStdString(v);
                    spdlog::trace("    Prop: {} = \"{}\" (string)", prop.get_name().to_string(), v);
                }
                
                propsList.append(propMap);
            }
            compMap["properties"] = propsList;
            componentsData.append(compMap);
        }
    });

    return componentsData;
}

bool GameModel::setProperty(int row, const QString& compName, const QString& propName, const QVariant& value) {
    if (row < 0 || row >= m_entities.size()) return false;

    auto entity = m_entities[row];
    if (!m_registry.valid(entity)) return false;

    bool found = false;
    std::string sCompName = compName.toStdString();
    std::string sPropName = propName.toStdString();

    spdlog::info("Attempting to set property '{}::{}' on entity {}", sCompName, sPropName, static_cast<uint32_t>(entity));

    for_each_component_type<ComponentList>([&](auto t) {
        using ComponentType = std::decay_t<decltype(t)>;
        if (found) return; // already done

        rttr::type type = rttr::type::get<ComponentType>();
        if (type.get_name().to_string() == sCompName) {
            if (m_registry.all_of<ComponentType>(entity)) {
                ComponentType& comp = m_registry.get<ComponentType>(entity);
                rttr::instance obj(comp);
                
                auto prop = type.get_property(sPropName);
                if (prop.is_valid()) {
                    // Convert QVariant to RTTR variant
                    rttr::variant rttrVal;
                    if (prop.get_type() == rttr::type::get<float>()) rttrVal = value.toFloat();
                    else if (prop.get_type() == rttr::type::get<double>()) rttrVal = value.toDouble();
                    else if (prop.get_type() == rttr::type::get<int>()) rttrVal = value.toInt();
                    else if (prop.get_type() == rttr::type::get<std::string>()) rttrVal = value.toString().toStdString();
                    
                    if (prop.set_value(obj, rttrVal)) {
                        found = true;
                        spdlog::info("SUCCESS: Set property '{}::{}' to {}", sCompName, sPropName, value.toString().toStdString());
                        
                        // Notify updates
                        if (compName == "NameComponent") {
                             QModelIndex idx = index(row);
                             emit dataChanged(idx, idx, {DisplayNameRole});
                        } else if (compName == "TransformComponent") {
                            // Update position roles if transform changed
                            QModelIndex idx = index(row);
                            emit dataChanged(idx, idx, {PositionXRole, PositionYRole});
                        }
                    } else {
                        spdlog::error("FAILED to set value for property '{}'", sPropName);
                    }
                } else {
                    spdlog::error("Property '{}' not found in component '{}'", sPropName, sCompName);
                }
            }
        }
    });

    if (!found) {
        spdlog::warn("Component '{}' or property '{}' not found, or set failed.", sCompName, sPropName);
    }

    return found;
}

void GameModel::addRandomEntity() {
    auto entity = m_registry.create();
    
    static std::mt19937 gen(std::random_device{}());
    std::uniform_real_distribution<float> dist(0, 500); // Increased range for visibility
    
    TransformComponent t;
    t.x = dist(gen);
    t.y = dist(gen);
    
    NameComponent n;
    n.name = "Entity " + std::to_string((uint32_t)entity);
    
    m_registry.emplace<TransformComponent>(entity, t);
    m_registry.emplace<NameComponent>(entity, n);
    
    if (dist(gen) > 250) { // 50% chance
        m_registry.emplace<HealthComponent>(entity);
    }

    beginInsertRows(QModelIndex(), m_entities.size(), m_entities.size());
    m_entities.push_back(entity);
    endInsertRows();

    spdlog::info("Created Entity ID: {} at ({}, {})", static_cast<uint32_t>(entity), t.x, t.y);
}

void GameModel::createSampleData() {
    spdlog::info("Creating sample data...");
    addRandomEntity();
    addRandomEntity();
    addRandomEntity();
}

void GameModel::runTestScenario() {
    spdlog::warn("--- STARTING SELF-TEST SCENARIO ---");
    
    if (m_entities.empty()) {
        spdlog::error("No entities to test!");
        return;
    }

    // 1. Inspect first entity
    spdlog::info("TEST: Inspecting entity 0");
    QVariantList data = getInspectorData(0);
    if (data.isEmpty()) spdlog::error("TEST FAIL: No inspector data returned");
    else spdlog::info("TEST PASS: Inspector data returned {} components", data.size());

    // 2. Modify Transform
    spdlog::info("TEST: Modifying Transform.x to 999.0");
    bool ok = setProperty(0, "TransformComponent", "x", 999.0f);
    if (ok) spdlog::info("TEST PASS: setProperty returned true");
    else spdlog::error("TEST FAIL: setProperty returned false");

    // 3. Verify modification
    auto& t = m_registry.get<TransformComponent>(m_entities[0]);
    if (std::abs(t.x - 999.0f) < 0.001f) spdlog::info("TEST PASS: Value actually changed to {}", t.x);
    else spdlog::error("TEST FAIL: Value is {}, expected 999.0", t.x);

    // 4. Modify Name
    spdlog::info("TEST: Modifying Name to 'TEST_NAME'");
    setProperty(0, "NameComponent", "name", QString("TEST_NAME"));
    auto& n = m_registry.get<NameComponent>(m_entities[0]);
    if (n.name == "TEST_NAME") spdlog::info("TEST PASS: Name changed to {}", n.name);
    else spdlog::error("TEST FAIL: Name is {}, expected TEST_NAME", n.name);

    spdlog::warn("--- SELF-TEST SCENARIO COMPLETE ---");
}
