#pragma once

#include <Arduino.h>

#include "../utils/vec.hpp"
#include "config.h"
#include "svg.hpp"
#include "../icons/index.hpp"

#include <TFT_eSPI.h>
#include <SD.h>

namespace Screen
{
    // The one-and-only TFT object (defined in index.cpp)
    extern TFT_eSPI tft;

    // Threshold for movement accumulation (milliseconds)
    extern int MOVEMENT_TIME_THRESHOLD;

    // Set backlight brightness (0–255)
    void setBrightness(byte b = 255);
    byte getBrightness();

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
