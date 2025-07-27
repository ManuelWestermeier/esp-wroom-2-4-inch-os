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
    size = {constrain(dimensions.x, minSize.x, maxSize.x), constrain(dimensions.y, minSize.y, maxSize.y)};

    bool colliding = true;
    int movedDown = 0;

    while (colliding && Windows::apps.size() > 0)
    {
        bool collides = false;
        for (auto &p : Windows::apps)
        {
            Window &w = *p;
            // if the window collides move it down
            if ((Rect{w.off + Vec{-1, -13}, w.size + Vec{12 + 2, 13}}.intersects(Rect{off + Vec{-1, -13}, size + Vec{12 + 2, 13}})))
            {
                collides = true;
            }
        }
        if (collides)
        {
            colliding = true;
            movedDown += 30;
            off.y += 30;
            break;
        }
    }

    for (auto &p : Windows::apps)
    {
        Window &w = *p;
        w.off.y -= movedDown;
    }

    if (_icon != nullptr)
    {
        memcpy(icon, _icon, sizeof(icon)); // Copies 144 * sizeof(uint16_t) = 288 Bytes
    }

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
