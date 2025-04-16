#pragma once
#include <glm/glm.hpp>
#include <QObject>
#include <QMetaType>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

namespace math
{

class ivec2 : public glm::ivec2 
{
    Q_GADGET
    
    Q_PROPERTY(int x READ getX WRITE setX)
    Q_PROPERTY(int y READ getY WRITE setY)

public:
    // Конструкторы
    ivec2() : glm::ivec2(0, 0) {}
    ivec2(int value) : glm::ivec2(value) {} // Инициализация обеих компонент одним значением
    ivec2(int x, int y) : glm::ivec2(x, y) {}
    ivec2(const glm::ivec2& other) : glm::ivec2(other) {}
    
    // Геттеры
    int getX() const { return this->glm::ivec2::x; }
    int getY() const { return this->glm::ivec2::y; }

    // Сеттеры
    void setX(int value) { this->glm::ivec2::x = value; }
    void setY(int value) { this->glm::ivec2::y = value; }

    // Арифметические операторы
    ivec2 operator+(const ivec2& other) const { return ivec2(this->x + other.x, this->y + other.y); }
    ivec2 operator-(const ivec2& other) const { return ivec2(this->x - other.x, this->y - other.y); }
    ivec2 operator*(const ivec2& other) const { return ivec2(this->x * other.x, this->y * other.y); }
    ivec2 operator/(const ivec2& other) const { return ivec2(this->x / other.x, this->y / other.y); }

    ivec2 operator+(int scalar) const { return ivec2(this->x + scalar, this->y + scalar); }
    ivec2 operator-(int scalar) const { return ivec2(this->x - scalar, this->y - scalar); }
    ivec2 operator*(int scalar) const { return ivec2(this->x * scalar, this->y * scalar); }
    ivec2 operator/(int scalar) const { return ivec2(this->x / scalar, this->y / scalar); }

    // Присваивающие операторы
    ivec2& operator+=(const ivec2& other) { this->x += other.x; this->y += other.y; return *this; }
    ivec2& operator-=(const ivec2& other) { this->x -= other.x; this->y -= other.y; return *this; }
    ivec2& operator*=(const ivec2& other) { this->x *= other.x; this->y *= other.y; return *this; }
    ivec2& operator/=(const ivec2& other) { this->x /= other.x; this->y /= other.y; return *this; }

    ivec2& operator+=(int scalar) { this->x += scalar; this->y += scalar; return *this; }
    ivec2& operator-=(int scalar) { this->x -= scalar; this->y -= scalar; return *this; }
    ivec2& operator*=(int scalar) { this->x *= scalar; this->y *= scalar; return *this; }
    ivec2& operator/=(int scalar) { this->x /= scalar; this->y /= scalar; return *this; }

    // Унарный оператор
    ivec2 operator-() const { return ivec2(-this->x, -this->y); }

    // Операторы сравнения
    bool operator==(const ivec2& other) const { return this->x == other.x && this->y == other.y; }
    bool operator!=(const ivec2& other) const { return !(*this == other); }

    // Оператор индексации
    int& operator[](int i) { return glm::ivec2::operator[](i); }
    const int& operator[](int i) const { return glm::ivec2::operator[](i); }

    // Побитовые операторы (доступны для целочисленных векторов в GLM)
    ivec2 operator&(const ivec2& other) const { return ivec2(this->x & other.x, this->y & other.y); }
    ivec2 operator|(const ivec2& other) const { return ivec2(this->x | other.x, this->y | other.y); }
    ivec2 operator^(const ivec2& other) const { return ivec2(this->x ^ other.x, this->y ^ other.y); }
    ivec2 operator<<(int shift) const { return ivec2(this->x << shift, this->y << shift); }
    ivec2 operator>>(int shift) const { return ivec2(this->x >> shift, this->y >> shift); }

    ivec2& operator&=(const ivec2& other) { this->x &= other.x; this->y &= other.y; return *this; }
    ivec2& operator|=(const ivec2& other) { this->x |= other.x; this->y |= other.y; return *this; }
    ivec2& operator^=(const ivec2& other) { this->x ^= other.x; this->y ^= other.y; return *this; }
    ivec2& operator<<=(int shift) { this->x <<= shift; this->y <<= shift; return *this; }
    ivec2& operator>>=(int shift) { this->x >>= shift; this->y >>= shift; return *this; }

    // Унарный побитовый оператор
    ivec2 operator~() const { return ivec2(~this->x, ~this->y); }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(ivec2, x, y);
};

// Внешние операторы (для операций со скаляром слева)
inline ivec2 operator+(int scalar, const ivec2& v) { return ivec2(scalar + v.getX(), scalar + v.getY()); }
inline ivec2 operator-(int scalar, const ivec2& v) { return ivec2(scalar - v.getX(), scalar - v.getY()); }
inline ivec2 operator*(int scalar, const ivec2& v) { return ivec2(scalar * v.getX(), scalar * v.getY()); }
inline ivec2 operator/(int scalar, const ivec2& v) { return ivec2(scalar / v.getX(), scalar / v.getY()); }



}//math

namespace std {
    template<>
    struct hash<math::ivec2> {
        size_t operator()(const math::ivec2& v) const {
            return hash<int>()(v.x) ^ (hash<int>()(v.y) << 1);
        }
    };
}

template<>
struct fmt::formatter<math::ivec2> : fmt::formatter<std::string>
{
    auto format(math::ivec2 iv2, format_context& ctx) const -> decltype(ctx.out())
    {
        return fmt::format_to(ctx.out(), "[{}, {}]", iv2.x, iv2.y);
    }
};


Q_DECLARE_METATYPE(math::ivec2)