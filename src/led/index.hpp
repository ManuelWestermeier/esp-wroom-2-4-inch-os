#pragma once

#include <Arduino.h>

namespace LED
{
    // PWM configuration
    extern int freq;
    extern int resolution;

    // Pins
    extern int pinR;
    extern int pinG;
    extern int pinB;

    // Channels
    extern int chR;
    extern int chG;
    extern int chB;

    void init();
    void rgb(uint8_t r, uint8_t g, uint8_t b);
    void off();
    
    void fadeTo(uint8_t r, uint8_t g, uint8_t b, uint16_t stepDelay = 10);
    
    void refresh(uint8_t r);
}
