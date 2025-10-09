#pragma once
#include <Arduino.h>

#include "../styles/global.hpp"
#include "../icons/index.hpp"
#include "../settings/index.hpp"
#include "../utils/shutdown.hpp"
#include "../screen/index.hpp"
#include "../screen/svg.hpp"
#include "./designer.hpp"

namespace SettingsMenu
{
    struct Slider
    {
        int x, y, w, h;
        uint16_t color;
        int value; // 0–255
        const char *label;
        NSVGimage *svg;
    };

    inline Slider sliders[] = {
        {40, 80, 240, 20, ACCENT2, 0, "Brightness", SVG::brightness},
        {40, 140, 240, 20, ACCENT3, 0, "Volume", SVG::volume}};

    inline bool touching = false;
    inline int activeSlider = -1;
    inline bool confirmed = false;
    inline bool canceled = false;

    void drawTitle()
    {
        using namespace Screen;
        auto &tft = Screen::tft;
        tft.fillScreen(BG);
        tft.setTextColor(PRIMARY, BG);
        tft.setTextSize(2);
        tft.setCursor(10, 10);
        tft.print("Settings");
        tft.fillRect(10, 32, 300, 2, ACCENT);
    }

    void drawSlider(const Slider &s, bool pressed = false)
    {
        using namespace Screen;
        auto &tft = Screen::tft;

        int sliderX = s.x;
        int sliderY = s.y;
        int sliderW = s.w;
        int sliderH = s.h;

        // Draw icon + label
        if (s.svg)
            drawSVGString(s.svg, sliderX - 30, sliderY - 8, 24, 24, TEXT);

        tft.setTextColor(TEXT, BG);
        tft.setTextSize(1);
        tft.setCursor(sliderX + sliderW + 8, sliderY - 4);
        tft.print(s.label);

        // Track
        tft.fillRoundRect(sliderX, sliderY, sliderW, sliderH, 5, PH);
        int fillWidth = map(s.value, 0, 255, 0, sliderW);
        tft.fillRoundRect(sliderX, sliderY, fillWidth, sliderH, 5, pressed ? PRESSED : s.color);

        // Knob
        tft.fillCircle(sliderX + fillWidth, sliderY + sliderH / 2, 6, pressed ? PRIMARY : AT);
    }

    void drawButtons()
    {
        using namespace Screen;
        auto &tft = Screen::tft;

        // OK button
        int okX = 60, okY = 200, okW = 80, okH = 30;
        tft.fillRoundRect(okX, okY, okW, okH, 6, PRIMARY);
        tft.setTextColor(AT, PRIMARY);
        tft.setTextSize(1);
        tft.setCursor(okX + 25, okY + 10);
        tft.print("OK");

        // Cancel button
        int cx = 180, cy = 200, cw = 80, ch = 30;
        tft.fillRoundRect(cx, cy, cw, ch, 6, DANGER);
        tft.setTextColor(AT, DANGER);
        tft.setCursor(cx + 15, cy + 10);
        tft.print("Cancel");
    }

    void drawUI()
    {
        drawTitle();
        for (auto &s : sliders)
            drawSlider(s);
        drawButtons();
    }

    void updateSlider(int index, int x)
    {
        auto &s = sliders[index];
        int newVal = map(x - s.x, 0, s.w, 0, 255);
        newVal = constrain(newVal, 0, 255);
        s.value = newVal;

        if (index == 0)
        {
            Settings::change();
            Screen::setBrightness(s.value);
        }
        else if (index == 1)
        {
            Settings::change();
        }

        drawSlider(s, true);
    }

    void handleTouch()
    {
        auto tp = Screen::getTouchPos();
        if (!tp.clicked && touching)
        {
            touching = false;
            activeSlider = -1;
            for (auto &s : sliders)
                drawSlider(s);
            return;
        }
        if (!tp.clicked)
            return;

        // Sliders
        if (!touching)
        {
            for (int i = 0; i < (int)(sizeof(sliders) / sizeof(Slider)); ++i)
            {
                auto &s = sliders[i];
                if (tp.y >= s.y - 10 && tp.y <= s.y + s.h + 10 &&
                    tp.x >= s.x && tp.x <= s.x + s.w)
                {
                    touching = true;
                    activeSlider = i;
                    break;
                }
            }
        }
        if (touching && activeSlider != -1)
        {
            updateSlider(activeSlider, tp.x);
            return;
        }

        // Buttons
        if (tp.y >= 200 && tp.y <= 230)
        {
            if (tp.x >= 60 && tp.x <= 140)
            {
                confirmed = true;
            }
            else if (tp.x >= 180 && tp.x <= 260)
            {
                canceled = true;
            }
        }
    }

    inline void open()
    {
        for (auto &s : sliders)
            s.value = (strcmp(s.label, "Brightness") == 0)
                          ? Settings::screenBrightNess
                          : Settings::volume;

        drawUI();
        confirmed = false;
        canceled = false;
    }

    inline void loop()
    {
        handleTouch();
    }
} // namespace SettingsMenu

// =============================
// PUBLIC ENTRY POINT
// =============================
inline void openSettings()
{
    using namespace SettingsMenu;

    Serial.println("Opening settings...");
    open();

    // Main blocking loop — until OK or Cancel is pressed
    while (!confirmed && !canceled)
    {
        handleTouch();
        delay(16); // smooth ~60 FPS polling
    }

    Screen::tft.fillScreen(BG);
}
