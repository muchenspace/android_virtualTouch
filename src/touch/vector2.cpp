#include "touch/vector2.h"

Vector2::Vector2()
{
    x = 0;
    y = 0;
}

Vector2::Vector2(float x, float y)
{
    this->x = x;
    this->y = y;
}

Vector2::Vector2(int x, int y)
{
    this->x = (float) x;
    this->y = (float) y;
}

Vector2::Vector2(const Vector2 &va)
{
    this->x = va.x;
    this->y = va.y;
}

Vector2 &Vector2::operator=(const Vector2 &other)
{
    // 防止自赋值
    if (this != &other)
    {
        this->x = other.x;
        this->y = other.y;
    }
    return *this;
}

Vector2 &Vector2::operator+=(float v)
{
    x += v;
    y += v;
    return *this;
}

Vector2 &Vector2::operator-=(float v)
{
    x -= v;
    y -= v;
    return *this;
}

Vector2 &Vector2::operator*=(float v)
{
    x *= v;
    y *= v;
    return *this;
}

Vector2 &Vector2::operator/=(float v)
{
    x /= v;
    y /= v;
    return *this;
}
