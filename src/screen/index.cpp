#include "index.hpp"

#include "../styles/global.hpp"
#include "../sys-apps/designer.hpp"

using namespace Screen;

// Define the single TFT instance
TFT_eSPI Screen::tft = TFT_eSPI(320, 240);

// Global threshold for movement timing
int Screen::MOVEMENT_TIME_THRESHOLD = 250;

// Internal state for touch calculations
static uint16_t touchY = 0, touchX = 0;
static uint16_t lastTouchY = UINT16_MAX, lastTouchX = 0;
static uint32_t lastTime = 0;
static byte screenBrightNess = 0;

void Screen::setBrightness(byte b)
{
    pinMode(TFT_BL, OUTPUT);
    analogWrite(TFT_BL, b);
    screenBrightNess = b;
}

byte Screen::getBrightness()
{
    return screenBrightNess;
}

void Screen::init(byte b)
{
    setBrightness(b);
    tft.init();
    tft.setRotation(2);
    
    applyColorPalette();
    
    tft.fillScreen(BG);
    tft.setTextColor(TEXT);
    tft.setTextSize(2);
    tft.setCursor(0, 0);

#ifdef TOUCH_CS
    tft.begin();
#endif
}

Screen::TouchPos Screen::getTouchPos()
{
    TouchPos pos{};
    uint32_t now = millis();

    pos.clicked = tft.getTouch(&touchY, &touchX);
    if (pos.clicked)
    {
        // Map raw touch to screen coords
        pos.x = touchX * 32 / 24;
        pos.y = (320 - touchY) * 24 / 32;

        // First touch ever: seed last positions
        if (lastTouchY == UINT16_MAX)
        {
            lastTouchY = pos.y;
            lastTouchX = pos.x;
        }

        uint32_t delta = now - lastTime;
        if (delta < MOVEMENT_TIME_THRESHOLD)
        {
            pos.move.x = pos.x - lastTouchX;
            pos.move.y = pos.y - lastTouchY;
        }
        else
        {
            pos.move = {0, 0};
        }

        lastTouchY = pos.y;
        lastTouchX = pos.x;
        lastTime = now;
    }

    return pos;
}

void Screen::drawImageFromSD(const char *filename, int x, int y)
{
    File f = SD.open(filename, FILE_READ);
    if (!f)
        return;
    uint16_t w = f.read() << 8 | f.read();
    uint16_t h = f.read() << 8 | f.read();
    for (int j = 0; j < h; j++)
    {
        for (int i = 0; i < w; i++)
        {
            uint16_t color = f.read() << 8 | f.read();
            tft.drawPixel(x + i, y + j, color);
        }
    }
    f.close();
}