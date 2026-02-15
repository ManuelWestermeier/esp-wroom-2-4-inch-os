#pragma once

#include <Arduino.h>
#include <TFT_eSPI.h>
#include <SD.h>

// Use the ESP32 Arduino FreeRTOS headers via freertos/...
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "../utils/vec.hpp"
#include "config.h"
#include "svg.hpp"
#include "../icons/index.hpp"
#include "../apps/index.hpp"

#define BRIGHTNESS_MIN 5

namespace Screen
{
    extern TFT_eSPI tft;
    extern int MOVEMENT_TIME_THRESHOLD;
    void setBrightness(byte b = 255, bool store = true);
    byte getBrightness();
    void init();

    struct TouchPos : Vec
    {
        bool clicked;
        Vec move;
    };

    bool isTouched();
    TouchPos getTouchPos();
    void drawImageFromSD(const char *filename, int x, int y);

    namespace SPI_Screen
    {
        void startScreen();
        void screenTask(void *pvParameters);

        // These are used internally by getTouchPos/isTouched to override touch from remote.
        void setRemoteDown(int16_t x, int16_t y);
        void setRemoteUp();
    }
}

using Screen::tft;