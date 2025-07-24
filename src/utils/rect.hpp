#pragma once

#include <Arduino.h>

#include "vec.hpp"

struct Rect
{
    Vec pos;        // obere linke Ecke
    Vec dimensions; // Breite und Höhe

    bool isIn(const Vec &point) const
    {
        return point.x >= pos.x &&
               point.x <= pos.x + dimensions.x &&
               point.y >= pos.y &&
               point.y <= pos.y + dimensions.y;
    }
};