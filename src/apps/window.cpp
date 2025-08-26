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

    name = windowName;

    // constrain size
    size = {
        constrain(dimensions.x, minSize.x, maxSize.x),
        constrain(dimensions.y, minSize.y, maxSize.y),
    };

    Serial.print("Constrained size: x=");
    Serial.print(size.x);
    Serial.print(", y=");
    Serial.println(size.y);

    // fixed screen size (center on 320x240)
    const int SCREEN_W = 320;
    const int SCREEN_H = 240;

    // center the new window on the screen
    Vec desiredOff = {(SCREEN_W - (int)size.x) / 2, (SCREEN_H - (int)size.y) / 2};
    Serial.print("Desired centered position: x=");
    Serial.print(desiredOff.x);
    Serial.print(", y=");
    Serial.println(desiredOff.y);

    // bounding rect helper (includes decoration margins like before)
    auto getBoundingRect = [&](const Vec &pos) -> Rect
    {
        return Rect{pos + Vec{-1, -13}, size + Vec{14, 13}};
    };

    Rect newRect = getBoundingRect(desiredOff);

    // fast path: no other windows
    if (Windows::apps.empty())
    {
        off = desiredOff;
        Serial.println("No existing windows — placed at center.");
        if (_icon != nullptr)
        {
            memcpy(icon, _icon, sizeof(icon));
        }
        return;
    }

    Serial.println("Resolving collisions by moving other windows away from the centered window...");

    const int margin = 4;          // small gap to avoid tight touching
    const int maxIterations = 200; // propagation safety cap
    int iterations = 0;
    bool changed = true;

    // Iteratively scan all windows and move any that intersect the new centered window.
    // Repeat until no moves are necessary or we hit maxIterations.
    while (changed && iterations < maxIterations)
    {
        changed = false;
        iterations++;

        for (auto &p : Windows::apps)
        {
            Window *w = p.get();
            // compute bounding rect for this window (based on its current off/size)
            Rect r = getBoundingRect(w->off);

            if (!r.intersects(newRect))
                continue; // no overlap, nothing to do

            // calculate overlap on X and Y
            int x1 = std::max(r.pos.x, newRect.pos.x);
            int x2 = std::min(r.pos.x + r.dimensions.x, newRect.pos.x + newRect.dimensions.x);
            int overlapX = x2 - x1;

            int y1 = std::max(r.pos.y, newRect.pos.y);
            int y2 = std::min(r.pos.y + r.dimensions.y, newRect.pos.y + newRect.dimensions.y);
            int overlapY = y2 - y1;

            if (overlapX <= 0 && overlapY <= 0)
                continue; // safety

            // choose the axis with larger overlap to move along (more natural)
            bool moveX = (overlapX > overlapY);
            int shift = std::max((moveX ? overlapX : overlapY) + margin, titleBarHeight);

            if (moveX)
            {
                int centerW = r.pos.x + r.dimensions.x / 2;
                int centerNew = newRect.pos.x + newRect.dimensions.x / 2;
                if (centerW >= centerNew)
                {
                    w->off.x += shift; // move right
                }
                else
                {
                    w->off.x -= shift; // move left
                }
            }
            else
            {
                int centerW = r.pos.y + r.dimensions.y / 2;
                int centerNew = newRect.pos.y + newRect.dimensions.y / 2;
                if (centerW >= centerNew)
                {
                    w->off.y += shift; // move down
                }
                else
                {
                    w->off.y -= shift; // move up
                }
            }

            // clamp so windows stay on screen
            w->off.x = constrain(w->off.x, 0, SCREEN_W - (int)w->size.x);
            w->off.y = constrain(w->off.y, 0, SCREEN_H - (int)w->size.y);

            Serial.print("Moved window '");
            Serial.print(w->name);
            Serial.print("' to x=");
            Serial.print(w->off.x);
            Serial.print(", y=");
            Serial.println(w->off.y);

            // mark that we changed something so we will re-scan (propagation)
            changed = true;
        }
    }

    if (iterations >= maxIterations)
    {
        Serial.println("Warning: collision propagation hit max iterations — layout may still overlap.");
    }

    // finally place the new window centered
    off = desiredOff;
    Serial.print("Placed new window at x=");
    Serial.print(off.x);
    Serial.print(", y=");
    Serial.println(off.y);

    // copy icon if provided
    if (_icon != nullptr)
    {
        Serial.println("Copying icon data...");
        memcpy(icon, _icon, sizeof(icon));
        Serial.println("Icon copied.");
    }
    else
    {
        Serial.println("No icon provided.");
    }
}

void Window::resizeSprite()
{
    // sprite.deleteSprite();
    // rightSprite.deleteSprite();
    // sprite.createSprite(size.x, size.y);
    // rightSprite.createSprite(resizeBoxSize, size.y - resizeBoxSize);
}
