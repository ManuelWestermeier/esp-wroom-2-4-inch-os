#pragma once

#include <Arduino.h>

#include "../styles/global.hpp"
#include "../icons/index.hpp"
#include "../audio/index.hpp"
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
        String svg;
    };

    inline Slider sliders[] = {
        {70, 80, 180, 12, ACCENT2, 0, "Brightness", SVG::brightness},
        {70, 130, 180, 12, ACCENT3, 0, "Volume", SVG::volume}};

    inline bool touching = false;
    inline int activeSlider = -1;
    inline bool goBack = false;

    struct Option
    {
        const char *label;
        String svg;
        uint16_t color;
    };

    inline Option options[] = {
        {"Design", SVG::design, ACCENT2},
        {"Shutdown", SVG::shutdown, DANGER}};

    // --- UI drawing ---
    void drawHeader()
    {
        using namespace Screen;
        auto &tft = Screen::tft;

        tft.fillScreen(BG);
        tft.setTextColor(TEXT, BG);
        tft.setTextSize(2);

        // Back icon (30x30)
        drawSVGString(SVG::back, 20, 21, 26, 26, TEXT);

        tft.setCursor(60, 25);
        tft.print("Settings");
    }

    void drawSlider(const Slider &s, bool pressed = false)
    {
        using namespace Screen;
        auto &tft = Screen::tft;

        int iconX = s.x - 35;
        int iconY = s.y - 6;

        drawSVGString(s.svg, iconX, iconY, 20, 20, TEXT);

        // label
        tft.setTextColor(TEXT, BG);
        tft.setTextSize(1);
        tft.setCursor(s.x - 5, s.y - 10);
        tft.print(s.label);

        // slider track
        tft.fillRoundRect(s.x, s.y, s.w, s.h, 4, PH);

        // fill
        int fillWidth = map(s.value, 0, 255, 0, s.w);
        tft.fillRoundRect(s.x, s.y, fillWidth, s.h, 4, pressed ? PRESSED : s.color);

        // knob
        tft.fillCircle(s.x + fillWidth, s.y + s.h / 2, 4, pressed ? PRIMARY : AT);
    }

    void drawOptions()
    {
        using namespace Screen;
        auto &tft = Screen::tft;

        int y = 200;
        int spacing = 140;
        int xStart = 40;

        for (int i = 0; i < 2; i++)
        {
            auto &opt = options[i];
            int bx = xStart + i * spacing;

            // icon
            if (opt.svg)
                drawSVGString(opt.svg, bx, y - 20, 28, 28, opt.color);

            // label
            tft.setTextColor(TEXT, BG);
            tft.setTextSize(1);
            tft.setCursor(bx + 35, y - 6);
            tft.print(opt.label);
        }
    }

    void drawUI()
    {
        drawHeader();
        for (auto &s : sliders)
            drawSlider(s);
        drawOptions();
    }

    // --- Logic ---
    void updateSlider(int index, int x)
    {
        auto &s = sliders[index];
        int newVal = map(x - s.x, 0, s.w, 0, 255);
        newVal = constrain(newVal, 0, 255);
        if (s.value == newVal)
            return;

        s.value = newVal;
        if (index == 0)
        {
            newVal = map(x - s.x, 0, s.w, BRIGHTNESS_MIN, 255);
            newVal = constrain(newVal, BRIGHTNESS_MIN, 255);
            Screen::setBrightness(newVal);
        }
        else if (index == 1)
        {
            Audio::setVolume(newVal);
        }

        drawSlider(s, true);
    }

    void handleTouch()
    {
        auto tp = Screen::getTouchPos();

        // touch release
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

        // Back button
        if (tp.x >= 5 && tp.x <= 40 && tp.y >= 5 && tp.y <= 35)
        {
            goBack = true;
            return;
        }

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

        // Options
        if (tp.y >= 180 && tp.y <= 230)
        {
            // Design
            if (tp.x >= 40 && tp.x <= 140)
            {
                openDesigner();
                drawUI();
                delay(200);
            }
            // Shutdown
            else if (tp.x >= 180 && tp.x <= 280)
            {
                shutdown();
                drawUI();
                delay(200);
            }
        }
    }

    inline void open()
    {
        for (auto &s : sliders)
            s.value = (strcmp(s.label, "Brightness") == 0)
                          ? Screen::getBrightness()
                          : Audio::getVolume();

        goBack = false;
        drawUI();
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

    // Block until user taps "Back"
    while (!goBack)
    {
        handleTouch();
        delay(16); // smooth ~60 FPS polling
    }

    Screen::tft.fillScreen(BG);
}
