#pragma once

#include <Arduino.h>

namespace Style
{
    namespace Colors
    {
        uint16_t bg = TFT_WHITE;
        uint16_t text = RGB(1, 1, 1);
        uint16_t primary = RGB(255, 240, 255);
        uint16_t accent = RGB(1, 1, 1);
        uint16_t danger = RGB(1, 1, 1);
        uint16_t pressed = RGB(1, 1, 1);
        uint16_t placeholder = RGB(200, 200, 200);
    }
}

#define BG Style::Colors::bg
#define TEXT Style::Colors::text
#define PRIMARY Style::Colors::primary
#define ACCENT Style::Colors::accent
#define DANGER Style::Colors::danger
#define PRESSED Style::Colors::pressed
#define PH Style::Colors::placeholder