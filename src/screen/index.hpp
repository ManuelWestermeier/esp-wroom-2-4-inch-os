#pragma once

#include "../utils/vec.hpp"
#include "./config.h"

#include <TFT_eSPI.h>
#include <SD.h>

namespace Screen
{
    // The one-and-only TFT object (defined in index.cpp)
    extern TFT_eSPI tft;

    // Threshold for movement accumulation (milliseconds)
    extern int MOVEMENT_TIME_THRESHOLD;

    // Set backlight brightness (0–255)
    void setBrightness(int b = 255);

    // Initialize display and touch
    void init(byte b = 200);

    // Touch data: absolute position + movement delta
    struct TouchPos : Vec
    {
        bool clicked;
        Vec move;
    };

    // Read the current touch state
    TouchPos getTouchPos();

    void drawImageFromSD(const char *filename, int x, int y);
}
