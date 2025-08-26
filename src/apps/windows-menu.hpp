#include "windows.hpp" // for Vec, MouseState, etc.
#include <vector>
#include <SD.h>

extern void executeApplication(const std::vector<String> &args);

struct AppRenderData
{
    String name;
    String path;
    uint16_t icon[20 * 20]; // fix 20x20

    bool loadIcon(const String &filename)
    {
        File f = SD.open(filename, FILE_READ);
        if (!f)
            return false;

        uint16_t w = (f.read() << 8) | f.read();
        uint16_t h = (f.read() << 8) | f.read();

        // Nur 20x20 akzeptieren
        if (w != 20 || h != 20)
        {
            f.close();
            return false;
        }

        for (int j = 0; j < 20; j++)
        {
            for (int i = 0; i < 20; i++)
            {
                uint16_t color = (f.read() << 8) | f.read();
                icon[j * 20 + i] = color;
            }
        }
        f.close();
        return true;
    }

    void drawIcon(int x, int y)
    {
        // schneller als drawPixel:
        Screen::tft.pushImage(x, y, 20, 20, icon);
    }
};

unsigned long menuUpdateTime = millis();

void Windows::drawMenu(Vec pos, Vec move, MouseState state)
{
    using Screen::tft;
    static int scrollYOff = 10;
    int itemHeight = 30; // mehr Platz, damit Icon+Text passen
    int itemWidth = 250;

    Rect screen = {{0, 0}, {320, 240}};
    Rect topSelect = {{10, 0}, {320, 50}};
    Rect programsView = {{10, topSelect.dimensions.y + 10},
                         {itemWidth, screen.dimensions.y - topSelect.dimensions.y}};

    // Apps nur alle 10s neu laden
    static std::vector<AppRenderData> apps;
    if (millis() - menuUpdateTime > 10000)
    {
        apps.clear();
        auto entries = SD_FS::readDir("/public/programs");
        for (auto &e : entries)
        {
            AppRenderData app;
            app.path = e.path();
            app.name = SD_FS::readFile(app.path + "/name.txt");
            app.loadIcon(app.path + "/icon-20x20.raw");
            apps.push_back(app);
        }
        menuUpdateTime = millis();
    }

    // Hintergrund oben
    tft.fillRect(topSelect.pos.x, topSelect.pos.y,
                 topSelect.dimensions.x, topSelect.dimensions.y,
                 RGB(255, 240, 255));

    tft.setTextSize(2);

    int i = 0;
    for (auto &app : apps)
    {
        i++;
        Rect appRect = {{10, scrollYOff + i * itemHeight},
                        {itemWidth, itemHeight}};

        if (appRect.intersects(topSelect))
            continue;
        if (!appRect.intersects(screen))
            continue;

        // Hintergrund für App-Eintrag
        tft.fillRoundRect(appRect.pos.x, appRect.pos.y,
                          appRect.dimensions.x, appRect.dimensions.y - 5,
                          5, RGB(255, 240, 255));

        // Icon links zeichnen
        app.drawIcon(appRect.pos.x + 5, appRect.pos.y + 5);

        // Name daneben
        tft.setCursor(appRect.pos.x + 30, appRect.pos.y + 8);
        tft.print(app.name);

        // Klick → App starten
        if (appRect.isIn(pos) && state == MouseState::Down)
        {
            executeApplication({app.path + "/", "Arg1", "Hi"});
            Windows::isRendering = true;
        }
    }

    // Scroll
    if (programsView.isIn(pos) && state == MouseState::Held)
    {
        scrollYOff = max(0, scrollYOff + move.y);
    }

    drawTime();
    delay(10);
}
