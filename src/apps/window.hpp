#pragma once
#include <Arduino.h>
#include "../screen/index.hpp"
#include "../utils/rect.hpp"
#include "../utils/vec.hpp"

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
    String name;
    TFT_eSprite sprite{&Screen::tft};
    MouseEvent lastEvent{MouseState::Up, {0, 0}, {0, 0}};

    static constexpr Vec minSize = {80, 60};
    static constexpr Vec maxSize = {240, 160};
    static constexpr int titleBarHeight = 12;
    static constexpr int closeBtnSize = 12;
    static constexpr int resizeBoxSize = 12;

    Rect dragArea() const { return {off - Vec{0, titleBarHeight}, {size.x, titleBarHeight}}; }
    Rect closeBtn() const { return {off + Vec{size.x - closeBtnSize, -titleBarHeight}, {closeBtnSize, closeBtnSize}}; }
    Rect resizeArea() const { return {off + size - Vec{resizeBoxSize, resizeBoxSize}, {resizeBoxSize, resizeBoxSize}}; }

    void init(const String &windowName, Vec position)
    {
        name = windowName;
        off = position;
        sprite.createSprite(size.x, size.y);
    }
    void resizeSprite()
    {
        sprite.deleteSprite();
        sprite.createSprite(size.x, size.y);
    }
};
