#include "index.hpp"

using namespace Screen;

// Define the single TFT instance
TFT_eSPI Screen::tft = TFT_eSPI(320, 240);

// Global threshold for movement timing
int Screen::MOVEMENT_TIME_THRESHOLD = 200;

// Internal state for touch calculations
static uint16_t touchY = 0, touchX = 0;
static uint16_t lastTouchY = UINT16_MAX, lastTouchX = 0;
static uint32_t lastTime = 0;

void Screen::setBrightness(int x)
{
    pinMode(TFT_BL, OUTPUT);
    analogWrite(TFT_BL, x);
}

void Screen::init()
{
    setBrightness(200);
    tft.init(RGB(25, 25, 25));
    tft.setRotation(2);
    tft.fillScreen(TFT_WHITE);

    tft.setTextColor(TFT_BLACK);
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
