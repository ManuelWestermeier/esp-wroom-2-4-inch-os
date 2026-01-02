#include "index.hpp"

#include "../screen/index.hpp"

#include <Adafruit_NeoPixel.h>

namespace LED
{
    constexpr int PIN = 4;      // NeoPixel data pin
    constexpr int NUM_LEDS = 1; // number of NeoPixels
    static Adafruit_NeoPixel strip(NUM_LEDS, PIN, NEO_GRB + NEO_KHZ800);

    static uint8_t curR = 0;
    static uint8_t curG = 0;
    static uint8_t curB = 0;

    static inline uint8_t applyBrightness(uint8_t v)
    {
        byte b = Screen::getBrightness(); // 0..255
        return (uint16_t(v) * b) >> 8;
    }

    void init()
    {
        strip.begin();
        strip.show(); // initialize all pixels to off
        off();
    }

    void rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        curR = r;
        curG = g;
        curB = b;

        uint8_t scaledR = applyBrightness(r);
        uint8_t scaledG = applyBrightness(g);
        uint8_t scaledB = applyBrightness(b);

        strip.setPixelColor(0, scaledR, scaledG, scaledB);
        strip.show();
    }

    void off()
    {
        rgb(0, 0, 0);
    }

    void fadeTo(uint8_t r, uint8_t g, uint8_t b, uint16_t stepDelay)
    {
        while (curR != r || curG != g || curB != b)
        {
            if (curR < r)
                curR++;
            else if (curR > r)
                curR--;
            if (curG < g)
                curG++;
            else if (curG > g)
                curG--;
            if (curB < b)
                curB++;
            else if (curB > b)
                curB--;

            uint8_t scaledR = applyBrightness(curR);
            uint8_t scaledG = applyBrightness(curG);
            uint8_t scaledB = applyBrightness(curB);

            strip.setPixelColor(0, scaledR, scaledG, scaledB);
            strip.show();
            delay(stepDelay);
        }
    }

    void refresh(uint8_t val)
    {
        // scale each color by val (0..255)
        uint8_t r = (uint16_t(curR) * val) >> 8;
        uint8_t g = (uint16_t(curG) * val) >> 8;
        uint8_t b = (uint16_t(curB) * val) >> 8;

        uint8_t scaledR = applyBrightness(r);
        uint8_t scaledG = applyBrightness(g);
        uint8_t scaledB = applyBrightness(b);

        strip.setPixelColor(0, scaledR, scaledG, scaledB);
        strip.show();
    }
}