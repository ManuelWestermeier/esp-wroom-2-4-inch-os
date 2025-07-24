#pragma once
#include <Arduino.h>

struct Vec
{
    int x;
    int y;

    void print() const
    {
        Serial.println(String("vec<x=") + x + ", y=" + y + ">");
    }

    Vec operator-(const Vec &other) const
    {
        return {x - other.x, y - other.y};
    }

    Vec operator+(const Vec &other) const
    {
        return {x + other.x, y + other.y};
    }

    Vec &operator+=(const Vec &other)
    {
        x += other.x;
        y += other.y;
        return *this;
    }
};
