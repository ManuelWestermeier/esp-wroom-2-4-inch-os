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
    name = windowName;

    // Constrain size
    size = {
        constrain(dimensions.x, minSize.x, maxSize.x),
        constrain(dimensions.y, minSize.y, maxSize.y)};

    // Screen center
    Vec screenCenter{240, 320}; // Example for 480x640 screen; adjust to your actual TFT size
    off = screenCenter - Vec{size.x / 2, size.y / 2};

    // Lambda to get bounding rect
    auto getBoundingRect = [&](Vec pos)
    {
        return Rect{pos + Vec{-1, -13}, size + Vec{14, 13}};
    };

    const int stepY = 14;
    const int maxTries = 5600 / stepY;
    int tries = 0;

    while (tries < maxTries)
    {
        bool collides = false;
        Rect thisRect = getBoundingRect(off);

        for (auto &p : Windows::apps)
        {
            Window &w = *p;
            if (thisRect.intersects(getBoundingRect(w.off)))
            {
                collides = true;
                break;
            }
        }

        if (!collides)
            break;

        // Move down if collision
        off.y += stepY;
        tries++;
    }

    // Optional: warn if no free space
    if (tries == maxTries)
    {
        Serial.println("Warning: could not find a non-colliding position!");
    }

    // Copy icon if provided
    if (_icon != nullptr)
    {
        memcpy(icon, _icon, sizeof(icon)); // 288 bytes
    }
}

void Window::resizeSprite()
{
    // sprite.deleteSprite();
    // rightSprite.deleteSprite();
    // sprite.createSprite(size.x, size.y);
    // rightSprite.createSprite(resizeBoxSize, size.y - resizeBoxSize);
}
