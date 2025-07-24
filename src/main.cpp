#include <Arduino.h>
#include "screen/index.hpp"

struct Rect
{
    Vec pos;        // obere linke Ecke
    Vec dimensions; // Breite und Höhe

    bool isIn(const Vec &point) const
    {
        return point.x >= pos.x &&
               point.x <= pos.x + dimensions.x &&
               point.y >= pos.y &&
               point.y <= pos.y + dimensions.y;
    }
};

struct Window
{
    // Create sprite (virtual screen)
    TFT_eSprite vScreen = TFT_eSprite(&Screen::tft);
    Vec off = {50, 50};
    Rect dragArea;
    String name = "Hello World";

    void init()
    {
        vScreen.createSprite(160, 90); // Uses ~20 KB RAM
        vScreen.setTextColor(TFT_BLACK);
    }

    int i = 0;
    void loop()
    {
        auto pos = Screen::getTouchPos();

        if (pos.clicked && dragArea.isIn(pos))
        {
            off.x += pos.move.x;
            off.y += pos.move.y;
        }

        if (pos.clicked)
        {
            Screen::tft.fillScreen(TFT_WHITE);
            i = 0;
        }

        Screen::tft.drawRect(off.x - 1, off.y - 12 - 1, 160 + 2, 90 + 2 + 12, TFT_BLACK);

        dragArea = {off, {160, 12}}; // 30x20 px gelbes Dragfeld oben links
        dragArea.pos += {0, -12};

        // on top left of the window
        Screen::tft.fillRect(dragArea.pos.x, dragArea.pos.y, dragArea.dimensions.x, dragArea.dimensions.y, RGB(220, 220, 250));

        Screen::tft.setTextSize(1);
        Screen::tft.setCursor(dragArea.pos.x + 2, dragArea.pos.y + 2);
        Screen::tft.print(name);
        Screen::tft.setTextSize(2);

        // Clear virtual screen
        vScreen.fillSprite(TFT_WHITE);

        // Draw something on the virtual screen
        vScreen.drawString("HELLO", 10, 10, 2); // font size 2

        // Push virtual screen to physical screen at offset
        vScreen.pushSprite(off.x, off.y);
    }
};

Window win;

void setup()
{
    Serial.begin(115200);
    Screen::init();
    win.init();
}

void loop()
{
    win.loop();
}
