#pragma once

class Vector2
{
public:
    Vector2();
    Vector2(float x, float y);
    Vector2(int x, int y);
    Vector2(const Vector2 &va);
    Vector2& operator=(const Vector2& other);
    Vector2& operator+=(float v);
    Vector2& operator-=(float v);
    Vector2& operator*=(float v);
    Vector2& operator/=(float v);
    float x{};
    float y{};
};
