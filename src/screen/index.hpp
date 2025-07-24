#pragma once

#include "./config.h"
#include "../utils/vec.hpp"

#include <TFT_eSPI.h> // Bodmer's TFT library
#include <SPI.h>

namespace Screen
{
    TFT_eSPI tft = TFT_eSPI(320, 240); // TFT instance

    void setBrightness(int x)
    {
        // set backlight
        pinMode(TFT_BL, OUTPUT);
        analogWrite(TFT_BL, x);
    }

    void init()
    {
        setBrightness(200);
        tft.init(RGB(25, 25, 25));
        tft.setRotation(2);
        tft.fillScreen(TFT_WHITE);

        tft.setTextColor(TFT_BLACK);
        tft.setTextSize(2);
        tft.setCursor(0, 0);

#ifdef TOUCH_CS
        tft.begin();
#endif
    }

    struct TouchPos : Vec
    {
        bool clicked;
        Vec move;
    };

    uint16_t touchY = 0, touchX = 0;
    uint16_t lastTouchY = 20000, lastTouchX = 0;

    auto lastTime = millis();
    TouchPos getTouchPos()
    {
        TouchPos pos = {};
        auto now = millis();

        pos.clicked = tft.getTouch(&touchY, &touchX);

        if (pos.clicked)
        {
            pos.x = touchX * 32 / 24;
            pos.y = (320 - touchY) * 24 / 32;

            if (lastTouchY == 20000)
            {
                lastTouchY = pos.y;
                lastTouchX = pos.x;
            }

            auto delta = now - lastTime;

            if (delta < 100)
            {
                pos.move.x = pos.x - lastTouchX;
                pos.move.y = pos.y - lastTouchY;
            }
            else
            {
                pos.move = {0, 0};
            }

            lastTouchY = pos.y;
            lastTouchX = pos.x;
            lastTime = now;
        }

        return pos;
    }
}