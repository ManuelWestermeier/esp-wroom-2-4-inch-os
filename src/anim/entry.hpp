#pragma once

#include "../screen/index.hpp"

void startAnimationMWOS()
{
    using Screen::tft;

    unsigned long start = millis();
    const unsigned long duration = 3000; // 3 seconds
    const int centerX = 160;
    const int centerY = 120;

    // Starfield
    const int starCount = 30;
    int starX[starCount];
    int starY[starCount];
    for (int i = 0; i < starCount; i++)
    {
        starX[i] = random(0, 320);
        starY[i] = random(0, 240);
    }

    tft.fillScreen(TFT_BLACK);

    while (millis() - start < duration)
    {
        float progress = (millis() - start) / (float)duration; // 0..1
        if (progress > 1)
            progress = 1;

        // Touch skip check
        uint16_t tx, ty;
        if (tft.getTouch(&tx, &ty))
        {
            progress = 1;
            start = millis() - duration; // force exit
        }

        // Background stars
        tft.fillScreen(TFT_BLACK);
        for (int i = 0; i < starCount; i++)
        {
            uint8_t brightness = random(120, 255);
            tft.drawPixel(starX[i], starY[i], tft.color565(brightness, brightness, brightness));
        }

        // Glow effect
        uint16_t glowColor = tft.color565(0, (uint8_t)(progress * 200), 255);
        int glowRadius = 50 + (int)(progress * 30);
        for (int r = glowRadius; r > 0; r -= 4)
        {
            tft.drawCircle(centerX, centerY, r, glowColor);
        }

        // Text slide-in with slight bounce
        int textX = (int)(-75 + (centerX - 60 + sin(progress * M_PI) * 5) * progress * 2);
        tft.setTextDatum(MC_DATUM);
        tft.setTextSize(3);
        tft.setTextColor(TFT_WHITE, TFT_BLACK);
        tft.drawString("MW 2.4 OS", textX + 40, centerY - 2);

        delay(16); // ~60 FPS
    }
}
