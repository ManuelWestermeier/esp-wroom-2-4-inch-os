#pragma once

#include <Arduino.h>
#include "esp_sleep.h"

#include "../anim/entry.hpp"
#include "../io/read-string.hpp"

inline void shutdown()
{
    Serial.println("Shutdown initiated...");
    if (readString("do you want to Shutdown/restart? y/n", "y").equalsIgnoreCase("y"))
    {
        const auto ANIM_TIME = 1500;

        startAnimationMWOS();

        unsigned long start = millis();
        while (millis() - start < ANIM_TIME)
        {
            uint32_t elapsed = millis() - start;
            uint8_t brightness = 255 - (elapsed * 255 / ANIM_TIME);
            Screen::setBrightness(brightness, false);
            delay(10);
        }
        Screen::setBrightness(0, false);

        Serial.println("ESP32 geht jetzt in Deep Sleep...");
        Serial.println("Drücke GPIO0 (BOOT-Taste), um aufzuwachen.");

        // Wake-up durch GPIO0 (LOW) aktivieren
        esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0); // 0 = LOW-Pegel weckt auf

        // Optional: interner Pull-up, falls Taste nach GND schaltet
        pinMode(GPIO_NUM_0, INPUT_PULLUP);

        delay(100);
        esp_deep_sleep_start();
    }
}