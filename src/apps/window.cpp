#include "window.hpp"

Rect Window::dragArea() const
{
    return {off - Vec{0, titleBarHeight}, {size.x + resizeBoxSize, titleBarHeight}};
}

Rect Window::closeBtn() const
{
    return {off + Vec{size.x, -titleBarHeight}, {closeBtnSize, closeBtnSize}};
}

Rect Window::resizeArea() const
{
    return {off + size - Vec{0, resizeBoxSize}, {resizeBoxSize, resizeBoxSize}};
}

void Window::init(const String &windowName, Vec position, Vec dimensions, uint16_t *_icon)
{
    Serial.println("=== Window::init called ===");
    Serial.print("Requested window name: ");
    Serial.println(windowName);
    Serial.print("Initial position: x=");
    Serial.print(position.x);
    Serial.print(", y=");
    Serial.println(position.y);
    Serial.print("Requested size: x=");
    Serial.print(dimensions.x);
    Serial.print(", y=");
    Serial.println(dimensions.y);

    name = windowName;
    off = position;
    size = {
        constrain(dimensions.x, minSize.x, maxSize.x),
        constrain(dimensions.y, minSize.y, maxSize.y),
    };

    Serial.print("Constrained size: x=");
    Serial.print(size.x);
    Serial.print(", y=");
    Serial.println(size.y);

    // Define this window's bounding rect with margin for decoration
    auto getBoundingRect = [&](Vec pos)
    {
        Rect rect{pos + Vec{-1, -13}, size + Vec{14, 13}}; // +14 accounts for border + margin
        Serial.print("Bounding rect at pos x=");
        Serial.print(rect.pos.x);
        Serial.print(", y=");
        Serial.print(rect.pos.y);
        Serial.print(" size x=");
        Serial.print(rect.dimensions.x);
        Serial.print(", y=");
        Serial.println(rect.dimensions.y);
        return rect;
    };

    // Try to find a non-colliding vertical spot
    const int stepY = 14;
    const int maxTries = 5600 / stepY;
    int tries = 0;
    int movedDown = 0;

    Serial.println("Starting collision detection loop...");
    while (tries < maxTries)
    {
        bool collides = false;
        Rect thisRect = getBoundingRect(off);

        Serial.print("Checking position y=");
        Serial.print(off.y);
        Serial.print(" (try ");
        Serial.print(tries);
        Serial.println(")");

        for (auto &p : Windows::apps)
        {
            Window &w = *p;
            Rect otherRect = getBoundingRect(w.off);
            if (thisRect.intersects(otherRect))
            {
                collides = true;
                Serial.print("Collision detected with window: ");
                Serial.println(w.name);
                break;
            }
        }

        if (!collides)
        {
            Serial.println("No collision detected, position OK.");
            break;
        }

        off.y += stepY;
        movedDown += stepY;
        Serial.print("Collision, moving down to y=");
        Serial.println(off.y);
        tries++;
    }

    if (tries == maxTries)
    {
        Serial.println("Warning: Could not find non-colliding position for window.");
    }
    else
    {
        Serial.print("Window positioned at y=");
        Serial.println(off.y);
    }

    // Move all other windows down to avoid overlap
    Serial.print("Adjusting other windows by moving them up by ");
    Serial.println(movedDown);
    for (auto &p : Windows::apps)
    {
        Window &w = *p;
        Serial.print("Moving window '");
        Serial.print(w.name);
        Serial.print("' from y=");
        Serial.print(w.off.y);
        w.off.y -= movedDown;
        Serial.print(" to y=");
        Serial.println(w.off.y);
    }

    // Set icon if provided
    if (_icon != nullptr)
    {
        Serial.println("Copying icon data...");
        memcpy(icon, _icon, sizeof(icon)); // 288 bytes
        Serial.println("Icon copied.");
    }
    else
    {
        Serial.println("No icon provided.");
    }

    // // Create sprites
    // Serial.println("Creating main sprite...");
    // sprite.createSprite(size.x, size.y);
    // Serial.println("Creating right sprite...");
    // rightSprite.createSprite(resizeBoxSize, size.y - resizeBoxSize);
}

void Window::resizeSprite()
{
    // sprite.deleteSprite();
    // rightSprite.deleteSprite();
    // sprite.createSprite(size.x, size.y);
    // rightSprite.createSprite(resizeBoxSize, size.y - resizeBoxSize);
}
