#pragma once
#include <Arduino.h>

namespace LED
{
    // ===== INITIALIZE =====
    void init();

    // ===== SET COLOR =====
    void rgb(uint8_t r, uint8_t g, uint8_t b);

    // ===== TURN OFF =====
    void off();

    // ===== FADE TO COLOR =====
    void fadeTo(uint8_t r, uint8_t g, uint8_t b, uint16_t stepDelay);

    // ===== REFRESH BRIGHTNESS =====
    void refresh(uint8_t brightness);
}
