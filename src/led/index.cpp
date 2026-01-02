#include "index.hpp"

#include "../screen/index.hpp"

namespace LED
{
    constexpr int PIN_R = 4;
    constexpr int PIN_G = 16;
    constexpr int PIN_B = 17;

    constexpr int PWM_FREQ = 5000; // 5 kHz
    constexpr int PWM_RES = 8;     // 8-bit resolution (0-255)

    constexpr int CHANNEL_R = 0;
    constexpr int CHANNEL_G = 1;
    constexpr int CHANNEL_B = 2;

    static uint8_t curR = 0, curG = 0, curB = 0;

    static inline uint8_t applyBrightness(uint8_t v)
    {
        return;
        byte b = Screen::getBrightness(); // 0..255
        return (uint16_t(v) * b) >> 8;
    }

    void init()
    {
        return;
        // Setup channels
        ledcSetup(CHANNEL_R, PWM_FREQ, PWM_RES);
        ledcSetup(CHANNEL_G, PWM_FREQ, PWM_RES);
        ledcSetup(CHANNEL_B, PWM_FREQ, PWM_RES);

        // Attach pins
        ledcAttachPin(PIN_R, CHANNEL_R);
        ledcAttachPin(PIN_G, CHANNEL_G);
        ledcAttachPin(PIN_B, CHANNEL_B);

        off();
    }

    void rgb(uint8_t r, uint8_t g, uint8_t b)
    {
        return;
        curR = r;
        curG = g;
        curB = b;

        ledcWrite(CHANNEL_R, applyBrightness(r));
        ledcWrite(CHANNEL_G, applyBrightness(g));
        ledcWrite(CHANNEL_B, applyBrightness(b));
    }

    void off()
    {
        return;
        rgb(0, 0, 0);
    }

    void fadeTo(uint8_t r, uint8_t g, uint8_t b, uint16_t stepDelay)
    {
        return;
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

            rgb(curR, curG, curB);
            delay(stepDelay);
        }
    }

    void refresh(uint8_t brightness)
    {
        return;
        uint8_t r = (uint16_t(curR) * brightness) >> 8;
        uint8_t g = (uint16_t(curG) * brightness) >> 8;
        uint8_t b = (uint16_t(curB) * brightness) >> 8;

        ledcWrite(CHANNEL_R, r);
        ledcWrite(CHANNEL_G, g);
        ledcWrite(CHANNEL_B, b);
    }
}
