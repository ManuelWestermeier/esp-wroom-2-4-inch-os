#pragma once

#include <Arduino.h>
#include "vec.hpp"

struct Rect
{
    Vec pos;        // obere linke Ecke
    Vec dimensions; // Breite und Höhe

    // Prüft, ob ein Punkt im Rechteck liegt
    bool isIn(const Vec &point) const
    {
        return point.x >= pos.x &&
               point.x <= pos.x + dimensions.x &&
               point.y >= pos.y &&
               point.y <= pos.y + dimensions.y;
    }

    // Prüft, ob sich zwei Rechtecke überschneiden
    bool intersects(const Rect &other) const
    {
        return !(pos.x + dimensions.x < other.pos.x ||
                 other.pos.x + other.dimensions.x < pos.x ||
                 pos.y + dimensions.y < other.pos.y ||
                 other.pos.y + other.dimensions.y < pos.y);
    }

    // Gibt das Zentrum des Rechtecks zurück
    Vec center() const
    {
        return Vec{
            pos.x + dimensions.x / 2,
            pos.y + dimensions.y / 2};
    }

    // Gibt die untere rechte Ecke zurück
    Vec bottomRight() const
    {
        return Vec{
            pos.x + dimensions.x,
            pos.y + dimensions.y};
    }

    // Überschneidungsbereich zweier Rechtecke
    Rect intersection(const Rect &other) const
    {
        int x1 = max(pos.x, other.pos.x);
        int y1 = max(pos.y, other.pos.y);
        int x2 = min(pos.x + dimensions.x, other.pos.x + other.dimensions.x);
        int y2 = min(pos.y + dimensions.y, other.pos.y + other.dimensions.y);

        if (x1 < x2 && y1 < y2)
        {
            return Rect{Vec{x1, y1}, Vec{x2 - x1, y2 - y1}};
        }
        return Rect{Vec{0, 0}, Vec{0, 0}}; // Kein Schnitt
    }
};
