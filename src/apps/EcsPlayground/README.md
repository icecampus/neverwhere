# EcsPlayground

Application for testing ECS (Entity Component System) architecture using the **EnTT** library and **Qt/QML**.

## Architecture: ECS Model Adapter

The application demonstrates the "ECS Model Adapter" pattern:

1.  **EnTT Registry (`entt::registry`)**: Stores all game entities and components (Pure Data).
2.  **QAbstractListModel Adapter (`EcsModel`)**:
    *   Wraps the EnTT registry.
    *   Maintains a cached list of active entities (`std::vector<entt::entity>`) to provide stable indices for QML.
    *   Exposes component data (Position, Name, etc.) via Qt Roles (`data()`, `roleNames()`).
    *   Handles the Game Loop (`tick()`) and updates components.
3.  **QML View**:
    *   Uses standard `ListView` or `Repeater` to display entities.
    *   Binds to the C++ model.
    *   Uses `Timer` to trigger the game loop tick.

## Features

*   **Entity Creation**: Add random entities with Position, Velocity, and Name components.
*   **Systems**: Simple movement and boundary bounce logic implemented in C++.
*   **Reactive UI**: QML automatically updates when the C++ model signals changes.
*   **Logging**: Integration with `spdlog`.

## Status

*   **EnTT**: Integrated and working.
*   **Rendering**: Currently using standard QML `Rectangle` items.
    *   *Note: Integration with Magnum graphics engine was attempted but reverted due to build system conflicts. Future graphics integration should prioritize a unified dependency management strategy.*
