#pragma once
#include <glm/glm.hpp>
#include <QObject>
#include <QMetaType>
#include <QVector2D>

namespace math
{

class vec2 : public glm::vec2 
{
    Q_GADGET
    
    Q_PROPERTY(float x READ getX WRITE setX)
    Q_PROPERTY(float y READ getY WRITE setY)

public:
    vec2() : glm::vec2(0, 0) {}
    vec2(float value) : glm::vec2(value) {}
    vec2(float x, float y) : glm::vec2(x, y) {}
    vec2(const glm::vec2& other) : glm::vec2(other) {}
    vec2(const QVector2D& other) : glm::vec2(other.x(), other.y()) {}

    // Геттеры
    float getX() const { return this->glm::vec2::x; }
    float getY() const { return this->glm::vec2::y; }

    // Сеттеры
    void setX(float value) { this->glm::vec2::x = value; }
    void setY(float value) { this->glm::vec2::y = value; }

    // Арифметические операторы
    vec2 operator+(const vec2& other) const { return vec2(this->x + other.x, this->y + other.y); }
    vec2 operator-(const vec2& other) const { return vec2(this->x - other.x, this->y - other.y); }
    vec2 operator*(const vec2& other) const { return vec2(this->x * other.x, this->y * other.y); }
    vec2 operator/(const vec2& other) const { return vec2(this->x / other.x, this->y / other.y); }

    vec2 operator+(float scalar) const { return vec2(this->x + scalar, this->y + scalar); }
    vec2 operator-(float scalar) const { return vec2(this->x - scalar, this->y - scalar); }
    vec2 operator*(float scalar) const { return vec2(this->x * scalar, this->y * scalar); }
    vec2 operator/(float scalar) const { return vec2(this->x / scalar, this->y / scalar); }

    // Присваивающие операторы
    vec2& operator+=(const vec2& other) { this->x += other.x; this->y += other.y; return *this; }
    vec2& operator-=(const vec2& other) { this->x -= other.x; this->y -= other.y; return *this; }
    vec2& operator*=(const vec2& other) { this->x *= other.x; this->y *= other.y; return *this; }
    vec2& operator/=(const vec2& other) { this->x /= other.x; this->y /= other.y; return *this; }

    vec2& operator+=(float scalar) { this->x += scalar; this->y += scalar; return *this; }
    vec2& operator-=(float scalar) { this->x -= scalar; this->y -= scalar; return *this; }
    vec2& operator*=(float scalar) { this->x *= scalar; this->y *= scalar; return *this; }
    vec2& operator/=(float scalar) { this->x /= scalar; this->y /= scalar; return *this; }

    // Унарные операторы
    vec2 operator-() const { return vec2(-this->x, -this->y); }

    // Операторы сравнения
    bool operator==(const vec2& other) const { return this->x == other.x && this->y == other.y; }
    bool operator!=(const vec2& other) const { return !(*this == other); }

    // Оператор доступа по индексу
    float& operator[](int i) { return glm::vec2::operator[](i); }
    const float& operator[](int i) const { return glm::vec2::operator[](i); }

     NLOHMANN_DEFINE_TYPE_INTRUSIVE(vec2, x, y);
};

// Внешние операторы (для симметрии со скалярами слева)
inline vec2 operator+(float scalar, const vec2& v) { return vec2(scalar + v.getX(), scalar + v.getY()); }
inline vec2 operator-(float scalar, const vec2& v) { return vec2(scalar - v.getX(), scalar - v.getY()); }
inline vec2 operator*(float scalar, const vec2& v) { return vec2(scalar * v.getX(), scalar * v.getY()); }
inline vec2 operator/(float scalar, const vec2& v) { return vec2(scalar / v.getX(), scalar / v.getY()); }

}//math


// template<>
// struct fmt::formatter<math::vec2> : fmt::formatter<std::string>
// {
//     auto format(math::vec2 iv2, format_context& ctx) const -> decltype(ctx.out())
//     {
//         return fmt::format_to(ctx.out(), "[{}, {}]", iv2.x, iv2.y);
//     }
// };


Q_DECLARE_METATYPE(math::vec2)