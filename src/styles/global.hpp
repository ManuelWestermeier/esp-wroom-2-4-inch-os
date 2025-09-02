#pragma once

#include <Arduino.h>

#include "../config.hpp"

namespace Style
{
    namespace Colors
    {
#ifdef DARKMODE
        // Backgrounds and general surfaces
        inline static uint16_t bg = RGB(18, 18, 28);      // deep dark blue-black
        inline static uint16_t primary = RGB(28, 28, 40); // slightly lighter dark surface

        // Text colors
        inline static uint16_t text = RGB(230, 230, 240);        // soft white for readability
        inline static uint16_t placeholder = RGB(120, 120, 140); // muted grey

        // Accents
        inline static uint16_t accent = RGB(100, 200, 255);     // bright cyan-blue
        inline static uint16_t accent2 = RGB(70, 150, 255);     // medium blue
        inline static uint16_t accent3 = RGB(50, 120, 220);     // deeper blue
        inline static uint16_t accentText = RGB(255, 255, 255); // white for contrast over accent

        // States
        inline static uint16_t pressed = RGB(40, 100, 180); // dark blue for pressed buttons
        inline static uint16_t danger = RGB(255, 100, 100); // soft red for alerts/errors
#else
        inline static uint16_t bg = RGB(245, 245, 255);
        inline static uint16_t text = RGB(2, 2, 4);
        inline static uint16_t primary = RGB(255, 240, 255);
        inline static uint16_t accent = RGB(30, 144, 255);
        inline static uint16_t accent2 = RGB(220, 220, 250);
        inline static uint16_t accent3 = RGB(180, 180, 255);
        inline static uint16_t accentText = TFT_WHITE;
        inline static uint16_t danger = RGB(255, 150, 150);
        inline static uint16_t pressed = accent;
        inline static uint16_t placeholder = RGB(200, 200, 200);
#endif
    }
}

#define BG Style::Colors::bg
#define TEXT Style::Colors::text
#define PRIMARY Style::Colors::primary
#define ACCENT Style::Colors::accent
#define ACCENT2 Style::Colors::accent2
#define ACCENT3 Style::Colors::accent3
#define DANGER Style::Colors::danger
#define PRESSED Style::Colors::pressed
#define PH Style::Colors::placeholder
#define AT Style::Colors::accentText
