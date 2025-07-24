#pragma once

#include <Arduino.h>
#include "../screen/index.hpp"
#include "../utils/rect.hpp"

struct Window
{
    TFT_eSprite vScreen = TFT_eSprite(&Screen::tft);

    Vec off;              // Fensterposition
    Vec size = {160, 90}; // Fenstergröße
    Rect dragArea;
    Rect closeBtn;
    Rect resizeArea;
    String name;

    bool isDragging = false;
    bool isResizing = false;

    Vec lastTouchPos;

    static constexpr Vec minSize = {80, 60};
    static constexpr Vec maxSize = {240, 160};
    static constexpr int titleBarHeight = 12;
    static constexpr int closeBtnSize = 12;
    static constexpr int resizeBoxSize = 12;

    void init(String windowName, Vec margin = {50, 50})
    {
        off = margin;
        name = windowName;

        vScreen.createSprite(size.x, size.y);
        vScreen.setTextColor(TFT_BLACK);
    }

    void drawTitleBar()
    {
        Rect frame = {off - Vec{1, titleBarHeight + 1}, size + Vec{2, titleBarHeight + 2}};
        Screen::tft.drawRect(frame.pos.x, frame.pos.y, frame.dimensions.x, frame.dimensions.y, TFT_BLACK);

        dragArea = {off - Vec{0, titleBarHeight}, {size.x, titleBarHeight}};
        Rect dragFill = dragArea;
        dragFill.dimensions.x -= closeBtnSize;
        Screen::tft.fillRect(dragFill.pos.x, dragFill.pos.y, dragFill.dimensions.x, dragFill.dimensions.y, RGB(220, 220, 250));

        Screen::tft.setTextSize(1);
        Screen::tft.setCursor(dragArea.pos.x + 2, dragArea.pos.y + 2);

        int len = (name.length() < (size.x - 20) / 6) ? name.length() : (size.x - 20) / 6;

        for (int i = 0; i < len; i++)
        {
            Screen::tft.print(name.charAt(i));
        }

        closeBtn = {Vec{dragArea.pos.x + size.x - closeBtnSize, dragArea.pos.y}, Vec{closeBtnSize, closeBtnSize}};
        Screen::tft.fillRect(closeBtn.pos.x, closeBtn.pos.y, closeBtn.dimensions.x, closeBtn.dimensions.y, RGB(255, 150, 150));
        Screen::tft.setCursor(closeBtn.pos.x + 4, closeBtn.pos.y + 2);
        Screen::tft.print("X");

        Screen::tft.setTextSize(2);
    }

    void drawResizeBox()
    {
        resizeArea = {off + size - Vec{resizeBoxSize, resizeBoxSize}, {resizeBoxSize, resizeBoxSize}};
        Screen::tft.fillRect(resizeArea.pos.x, resizeArea.pos.y, resizeArea.dimensions.x, resizeArea.dimensions.y, RGB(180, 180, 255));
    }

    void drawContent()
    {
        vScreen.fillSprite(TFT_WHITE);
        vScreen.drawString("HELLO", 10, 10, 2);
        vScreen.pushSprite(off.x, off.y);
    }

    void resizeWindow(Vec delta)
    {
        size += delta;
        size.x = constrain(size.x, minSize.x, maxSize.x);
        size.y = constrain(size.y, minSize.y, maxSize.y);
        vScreen.deleteSprite();
        vScreen.createSprite(size.x, size.y);
    }

    void exit()
    {
        // Fenster schließen
    }

    void loop()
    {
        static bool wasTouchedLastFrame = false;

        auto pos = Screen::getTouchPos();

        if (pos.clicked)
        {
            // clear the screen
            Screen::tft.fillScreen(TFT_WHITE);

            // Erste Berührung
            if (!wasTouchedLastFrame)
            {
                if (closeBtn.isIn(pos))
                {
                    exit();
                    return;
                }
                if (dragArea.isIn(pos))
                {
                    isDragging = true;
                }
                else if (resizeArea.isIn(pos))
                {
                    isResizing = true;
                }
            }

            if (isDragging)
            {
                off += pos.move;
            }
            else if (isResizing)
            {
                resizeWindow(pos.move);
            }

            lastTouchPos = {pos.x, pos.y};
            wasTouchedLastFrame = true;
        }
        else
        {
            // Kein Touch mehr
            isDragging = false;
            isResizing = false;
            wasTouchedLastFrame = false;
        }

        // Zeichnen
        drawTitleBar();
        drawContent();
        drawResizeBox();
    }
};
