#pragma once
#include <Arduino.h>

#include "../screen/index.hpp"
#include "../utils/rect.hpp"
#include "../utils/vec.hpp"

#include "windows.hpp"

enum class MouseState
{
    Down,
    Held,
    Up
};

struct MouseEvent
{
    MouseState state;
    Vec pos;
    Vec move;
};

struct Window
{
    Vec off = {0, 0};
    Vec size = {160, 90};
    String name = "";
    TFT_eSprite sprite{&Screen::tft};
    TFT_eSprite rightSprite{&Screen::tft};

    MouseEvent lastEvent{MouseState::Up, {0, 0}, {0, 0}};
    MouseEvent lastEventRightSprite{MouseState::Up, {0, 0}, {0, 0}};
    bool closed = false;

#include "icon.hpp"

    static constexpr Vec minSize = {40, 30};
    static constexpr Vec maxSize = {240, 160};
    static constexpr int titleBarHeight = 12;
    static constexpr int closeBtnSize = 12;
    static constexpr int resizeBoxSize = 12;

    Rect dragArea() const { return {off - Vec{0, titleBarHeight}, {size.x + resizeBoxSize, titleBarHeight}}; }
    Rect closeBtn() const { return {off + Vec{size.x, -titleBarHeight}, {closeBtnSize, closeBtnSize}}; }
    Rect resizeArea() const { return {off + size - Vec{0, resizeBoxSize}, {resizeBoxSize, resizeBoxSize}}; }

    void init(const String &windowName = "Untitled Window", Vec position = {20, 20}, Vec dimensions = {160, 90}, uint16_t *_icon = nullptr)
    {
        name = windowName;
        off = position;
        size = {constrain(dimensions.x, minSize.x, maxSize.x), constrain(dimensions.y, minSize.y, maxSize.y)};

        bool colliding = true;
        int movedDown = 0;

        while (colliding)
        {
            for (auto &p : Windows::apps)
            {
                Window &w = *p;
                // if the window collides move it down
                if (Rect{w.off + Vec{-1, -13}, w.size + Vec{12 + 2, 13}}.intersects(Rect{off + Vec{-1, -13}, size + Vec{12 + 2, 13}}))
                {
                    colliding = true;
                    movedDown += 30;
                    off.y += 30;
                    break;
                }
            }
        }

        for (auto &p : Windows::apps)
        {
            Window &w = *p;
            w.off.y -= movedDown;
        }

        if (_icon != nullptr)
        {
            memcpy(icon, _icon, sizeof(icon)); // kopiert 144 * sizeof(uint16_t) = 288 Bytes
        }

        sprite.createSprite(size.x, size.y);
        rightSprite.createSprite(resizeBoxSize, size.y - resizeBoxSize);

        Screen::tft.fillScreen(RGB(245, 245, 255));
    }

    void resizeSprite()
    {
        sprite.deleteSprite();
        sprite.createSprite(size.x, size.y);
        rightSprite.deleteSprite();
        rightSprite.createSprite(resizeBoxSize, size.y - resizeBoxSize);
    }
};
