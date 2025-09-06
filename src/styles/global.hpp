#pragma once

#include <Arduino.h>

namespace Style
{
        namespace Colors
        {
                // Backgrounds and general surfaces
                extern uint16_t bg;
                extern uint16_t primary;

                // Text colors
                extern uint16_t text;
                extern uint16_t placeholder;

                // Accents
                extern uint16_t accent;
                extern uint16_t accent2;
                extern uint16_t accent3;
                extern uint16_t accentText;

                // States
                extern uint16_t pressed;
                extern uint16_t danger;
        }
}

// Macros
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
