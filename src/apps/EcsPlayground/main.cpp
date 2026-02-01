#include <iostream>
#include <string>
#include <entt/entt.hpp>

// --- Components ---
// Simple data structures (PODs)

struct Position {
    float x;
    float y;
};

struct Velocity {
    float dx;
    float dy;
};

struct Name {
    std::string value;
};

// --- Systems ---
// Logic that operates on components

void movementSystem(entt::registry &registry) {
    // Select entities that have both Position and Velocity
    auto view = registry.view<Position, const Velocity>();

    // Iterate and update
    view.each([](Position &pos, const Velocity &vel) {
        pos.x += vel.dx;
        pos.y += vel.dy;
    });
}

void renderSystem(const entt::registry &registry) {
    // Select entities with Name and Position
    auto view = registry.view<const Name, const Position>();

    view.each([](const Name &name, const Position &pos) {
        std::cout << "Entity: " << name.value 
                  << " | Pos: (" << pos.x << ", " << pos.y << ")" 
                  << std::endl;
    });
}

int main() {
    std::cout << "Initializing EnTT ECS Demo..." << std::endl;

    entt::registry registry;

    // Create entities
    for(int i = 0; i < 5; ++i) {
        const auto entity = registry.create();
        
        // Add components
        registry.emplace<Position>(entity, static_cast<float>(i * 10), static_cast<float>(i * 10));
        registry.emplace<Name>(entity, "Object_" + std::to_string(i));

        // Add Velocity only to even entities
        if(i % 2 == 0) {
            registry.emplace<Velocity>(entity, 1.0f, 0.5f);
        }
    }

    std::cout << "\n--- Initial Frame ---" << std::endl;
    renderSystem(registry);

    std::cout << "\n--- Simulating 3 frames ---" << std::endl;
    for(int i = 0; i < 3; ++i) {
        movementSystem(registry);
        std::cout << "Frame " << (i + 1) << ":" << std::endl;
        renderSystem(registry);
    }

    return 0;
}
