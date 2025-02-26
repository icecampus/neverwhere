#pragma once
#include "ivec.h"
#include "vec.h"


//Vec2Factory
class MathFactory : public QObject
{
    Q_OBJECT
public:
    explicit MathFactory(QObject* parent = nullptr) : QObject(parent) {}
    Q_INVOKABLE math::ivec2 ivec2(float x, float y) { return math::ivec2(x, y); }
    Q_INVOKABLE math::vec2 vec2(float x, float y) { return math::vec2(x, y); }
};
