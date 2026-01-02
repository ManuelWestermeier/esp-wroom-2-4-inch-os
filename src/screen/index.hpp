#pragma once

#include <Arduino.h>

#include "../utils/vec.hpp"
#include "config.h"
#include "svg.hpp"
#include "../icons/index.hpp"

#include <TFT_eSPI.h>
#include <SD.h>

#define BRIGHTNESS_MIN 5

namespace Screen
{
    // The one-and-only TFT object (defined in index.cpp)
    extern TFT_eSPI tft;

    // Threshold for movement accumulation (milliseconds)
    extern int MOVEMENT_TIME_THRESHOLD;

    // Set backlight brightness (0–255)
    void setBrightness(byte b = 255, bool store = true);
    byte getBrightness();

    // Initialize display and touch
    void init();

    // Touch data: absolute position + movement delta
    struct TouchPos : Vec
    {
        bool clicked;
        Vec move;
    };

    // Read the current touch state
    bool isTouched();
    TouchPos getTouchPos();

    void drawImageFromSD(const char *filename, int x, int y);
}

using Screen::tft;