#pragma once

#include <Arduino.h>

#include "../screen/index.hpp"

namespace Settings
{
    uint8_t volume = 100;
    uint8_t screenBrightNess = 100;

    void load() {}
    void change()
    {
        Screen::setBrightness(screenBrightNess);
        // Audio::setVolume(volume);
    }
}