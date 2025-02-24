#pragma once
#include <unordered_map>
#include <glm/glm.hpp>
struct IVec2Hash
{
    std::size_t operator()(const glm::ivec2& v) const 
    {
        std::size_t seed = 0;
        hash_combine(seed, v.x);
        hash_combine(seed, v.y);
        return seed;
    }

private:
    template <typename T>
    static void hash_combine(std::size_t& seed, const T& val) {
        std::hash<T> hasher;
        seed ^= hasher(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    }
};

