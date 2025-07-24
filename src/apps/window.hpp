#pragma once

#include <Arduino.h>
#include "../screen/index.hpp"
#include "../utils/rect.hpp"

struct Window
{
    TFT_eSprite vScreen = TFT_eSprite(&Screen::tft);

    Vec off;       // Fensterposition
    Rect dragArea; // Bereich zum Ziehen
    String name;   // Fenstertitel

    static constexpr Vec windowSize = {160, 90};
    static constexpr int titleBarHeight = 12;
    static constexpr int closeBtnSize = 12;

    void init(String windowName, Vec margin = {50, 50})
    {
        off = margin;
        name = windowName;

        vScreen.createSprite(windowSize.x, windowSize.y); // ~20 KB RAM
        vScreen.setTextColor(TFT_BLACK);
    }

    void drawTitleBar()
    {
        // Fensterrahmen
        Screen::tft.drawRect(
            off.x - 1, off.y - titleBarHeight - 1,
            windowSize.x + 2, windowSize.y + 2 + titleBarHeight,
            TFT_BLACK);

        // Drag-Area
        dragArea = {off + Vec{0, -titleBarHeight}, {windowSize.x, titleBarHeight}};
        Screen::tft.fillRect(
            dragArea.pos.x, dragArea.pos.y,
            dragArea.dimensions.x - closeBtnSize, dragArea.dimensions.y,
            RGB(220, 220, 250));

        // Fenstertitel
        Screen::tft.setTextSize(1);
        Screen::tft.setCursor(dragArea.pos.x + 2, dragArea.pos.y + 2);
        Screen::tft.print(name);

        // Schließen-Button
        Screen::tft.fillRect(
            dragArea.pos.x + windowSize.x - closeBtnSize, dragArea.pos.y,
            closeBtnSize, closeBtnSize,
            RGB(255, 180, 180));
        Screen::tft.setCursor(dragArea.pos.x + windowSize.x - closeBtnSize + 4, dragArea.pos.y + 2);
        Screen::tft.print("X");

        Screen::tft.setTextSize(2); // Wieder Standardgröße
    }

    void drawContent()
    {
        vScreen.fillSprite(TFT_WHITE);
        vScreen.drawString("HELLO", 10, 10, 2); // Beispielinhalt
        vScreen.pushSprite(off.x, off.y);
    }

    void loop()
    {
        auto pos = Screen::getTouchPos();

        // Dragging
        if (pos.clicked && dragArea.isIn(pos))
        {
            off += pos.move;
        }

        // Beispiel: Bildschirm löschen bei Klick
        if (pos.clicked)
        {
            Screen::tft.fillScreen(TFT_WHITE);
        }

        drawTitleBar();
        drawContent();
    }
};
