#pragma once

#include <Arduino.h>

namespace Style
{
    namespace Colors
    {
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