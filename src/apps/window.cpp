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
    off = position;
    size = {
        constrain(dimensions.x, minSize.x, maxSize.x),
        constrain(dimensions.y, minSize.y, maxSize.y)};

    // Define this window's bounding rect with margin for decoration
    auto getBoundingRect = [&](Vec pos)
    {
        return Rect{pos + Vec{-1, -13}, size + Vec{14, 13}}; // +14 accounts for border + margin
    };

    // Try to find a non-colliding vertical spot
    const int stepY = 30;
    const int maxTries = 50; // Prevent infinite loop
    int tries = 0;
    int movedDown = 0;

    while (tries < maxTries)
    {
        bool collides = false;
        Rect thisRect = getBoundingRect(off);

        for (auto &p : Windows::apps)
        {
            Window &w = *p;
            Rect otherRect = getBoundingRect(w.off);
            if (thisRect.intersects(otherRect))
            {
                collides = true;
                break;
            }
        }

        if (!collides)
            break;

        off.y += stepY;
        movedDown += stepY;
        tries++;
    }

    for (auto &p : Windows::apps)
    {
        Window &w = *p;
        w.off.y -= movedDown;
    }

    // Optional: log if the window couldn't find space
    if (tries == maxTries)
    {
        Serial.println("Warning: Could not find non-colliding position for window.");
    }

    // Set icon if provided
    if (_icon != nullptr)
    {
        memcpy(icon, _icon, sizeof(icon)); // 288 bytes
    }

    // Create sprites
    sprite.createSprite(size.x, size.y);
    rightSprite.createSprite(resizeBoxSize, size.y - resizeBoxSize);
    Screen::tft.fillScreen(RGB(245, 245, 255));
}

void Window::resizeSprite()
{
    sprite.deleteSprite();
    sprite.createSprite(size.x, size.y);
    rightSprite.deleteSprite();
    rightSprite.createSprite(resizeBoxSize, size.y - resizeBoxSize);
}
