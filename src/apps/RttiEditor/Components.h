#pragma once

#include <string>
#include <rttr/type>

struct TransformComponent {
    float x = 0.0f;
    float y = 0.0f;
    
    RTTR_ENABLE()
};

struct NameComponent {
    std::string name;
    
    RTTR_ENABLE()
};

struct HealthComponent {
    int current = 100;
    int max = 100;
    
    RTTR_ENABLE()
};
