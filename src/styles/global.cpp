#include "global.hpp"

#define DARKMODE

#ifndef RGB
#define RGB(r, g, b) ((uint16_t)(((r & 0xF8) << 8) | \
                                 ((g & 0xFC) << 3) | \
                                 ((b & 0xF8) >> 3)))
#endif

namespace Style::Colors
{
#ifdef DARKMODE
    uint16_t bg = RGB(18, 18, 28);
    uint16_t primary = RGB(28, 28, 40);
    uint16_t text = RGB(230, 230, 240);
    uint16_t placeholder = RGB(120, 120, 140);
    uint16_t accent = RGB(100, 200, 255);
    uint16_t accent2 = RGB(70, 150, 255);
    uint16_t accent3 = RGB(50, 120, 220);
    uint16_t accentText = RGB(255, 255, 255);
    uint16_t pressed = RGB(40, 100, 180);
    uint16_t danger = RGB(255, 100, 100);
#else
    uint16_t bg = RGB(245, 245, 255);
    uint16_t text = RGB(2, 2, 4);
    uint16_t primary = RGB(255, 240, 255);
    uint16_t accent = RGB(30, 144, 255);
    uint16_t accent2 = RGB(220, 220, 250);
    uint16_t accent3 = RGB(180, 180, 255);
    uint16_t accentText = TFT_WHITE;
    uint16_t danger = RGB(255, 150, 150);
    uint16_t pressed = accent;
    uint16_t placeholder = RGB(200, 200, 200);
#endif
}
