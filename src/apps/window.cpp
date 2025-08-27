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
    // name
    name = windowName;
    // Constrain size
    size = {
        constrain(dimensions.x, minSize.x, maxSize.x),
        constrain(dimensions.y, minSize.y, maxSize.y),
    };

    off = {
        constrain(position.x, 0, 320 - size.x),
        constrain(position.y, 0, 240 - size.y),
    };
    // Copy icon if provided
    if (_icon != nullptr)
    {
        memcpy(icon, _icon, sizeof(icon)); // 288 bytes
    }

    // Lambda to get bounding rect
    auto getBoundingRect = [&](Vec pos, Vec size)
    {
        return Rect{pos + Vec{-1, -13}, size + Vec{14, 13}};
    };

    const int step = 20;
    while (1)
    {
        bool collides = false;
        Rect thisRect = getBoundingRect(off, size);

        for (auto &p : Windows::apps)
        {
            Window &w = *p;
            if (thisRect.intersects(getBoundingRect(w.off, w.size)))
            {
                collides = true;
                break;
            }
        }

        if (!collides)
            break;

        for (auto &p : Windows::apps)
        {
            p->off.y -= step;
        }
    }
}

void Window::resizeSprite()
{
    // sprite.deleteSprite();
    // rightSprite.deleteSprite();
    // sprite.createSprite(size.x, size.y);
    // rightSprite.createSprite(resizeBoxSize, size.y - resizeBoxSize);
}
