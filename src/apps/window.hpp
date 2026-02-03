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
    MouseEvent lastEvent{MouseState::Up, {0, 0}, {0, 0}};
    MouseEvent lastEventRightSprite{MouseState::Up, {0, 0}, {0, 0}};
    bool closed = false;
    bool wasClicked = false;
    bool needRedraw = false;

#include "icon.hpp"

    static constexpr Vec minSize = {40, 30};
    static constexpr Vec maxSize = {320, 240};
    static constexpr int titleBarHeight = 12;
    static constexpr int closeBtnSize = 12;
    static constexpr int resizeBoxSize = 12;

    Rect dragArea() const;
    Rect closeBtn() const;
    Rect resizeArea() const;

    void init(const String &windowName = "Untitled Window", Vec position = {20, 20}, Vec dimensions = {160, 90}, uint16_t *_icon = nullptr);
};
