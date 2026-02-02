#include "Components.h"
#include <rttr/registration>

RTTR_REGISTRATION
{
    using namespace rttr;

    registration::class_<TransformComponent>("TransformComponent")
        .constructor<>()
        .property("x", &TransformComponent::x)
        .property("y", &TransformComponent::y);

    registration::class_<NameComponent>("NameComponent")
        .constructor<>()
        .property("name", &NameComponent::name);

    registration::class_<HealthComponent>("HealthComponent")
        .constructor<>()
        .property("current", &HealthComponent::current)
        .property("max", &HealthComponent::max);
}
