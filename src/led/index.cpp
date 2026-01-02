#include "index.hpp"
#include "../screen/index.hpp" // For Screen::getBrightness()

namespace LED
{
    constexpr int PIN_R = 4;
    constexpr int PIN_G = 16;
    constexpr int PIN_B = 17;

    static uint8_t curR = 0;
    static uint8_t curG = 0;
    static uint8_t curB = 0;

    // ===== BRIGHTNESS SCALING =====
    static inline uint8_t applyBrightness(uint8_t v)
    {
        byte b = Screen::getBrightness(); // 0..255
        return (uint16_t(v) * b) >> 8;
    }

    // ===== INITIALIZE =====
    void init()
    {
        pinMode(PIN_R, OUTPUT);
        pinMode(PIN_G, OUTPUT);
        pinMode(PIN_B, OUTPUT);

        off();
    }

    // ===== SET COLOR =====
    void rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        curR = r; curG = g; curB = b;

        analogWrite(PIN_R, applyBrightness(r));
        analogWrite(PIN_G, applyBrightness(g));
        analogWrite(PIN_B, applyBrightness(b));
    }

    // ===== TURN OFF =====
    void off()
    {
        rgb(0, 0, 0);
    }

    // ===== FADE TO COLOR =====
    void fadeTo(uint8_t r, uint8_t g, uint8_t b, uint16_t stepDelay)
    {
        while(curR != r || curG != g || curB != b)
        {
            if(curR < r) curR++; else if(curR > r) curR--;
            if(curG < g) curG++; else if(curG > g) curG--;
            if(curB < b) curB++; else if(curB > b) curB--;

            rgb(curR, curG, curB);
            delay(stepDelay);
        }
    }

    // ===== REFRESH BRIGHTNESS =====
    void refresh(uint8_t brightness)
    {
        uint8_t r = (uint16_t(curR) * brightness) >> 8;
        uint8_t g = (uint16_t(curG) * brightness) >> 8;
        uint8_t b = (uint16_t(curB) * brightness) >> 8;

        analogWrite(PIN_R, r);
        analogWrite(PIN_G, g);
        analogWrite(PIN_B, b);
    }
}
