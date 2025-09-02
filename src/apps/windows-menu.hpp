#include "windows.hpp" // for Vec, MouseState, etc.
#include <vector>
#include <SD.h>

extern void executeApplication(const std::vector<String> &args);

struct AppRenderData
{
    String name;
    ENC_FS::Path path;
    uint16_t icon[20 * 20]; // nur ein Icon-Buffer
    bool hasIcon = false;

    bool loadIcon(const ENC_FS::Path &filename, uint16_t bgColor = PRIMARY, uint8_t radius = 5)
    {

        if (!ENC_FS::exists(filename))
            return false;

        // defensive: prüfen, ob mindestens 4 bytes für w/h da sind
        if (ENC_FS::getFileSize(filename) != 804) // 400*2+4bytes
        {
            Serial.println("IFCONFS dont match 804: " + ENC_FS::path2Str(filename) + " " + ENC_FS::getFileSize(filename));
            return false;
        }

        auto data = ENC_FS::readFile(filename, 4, 804);

        Serial.println(data.size());
        if (data.size() != 800)
            return false;

        int bI = 0;
        // Bilddaten einlesen
        for (int j = 0; j < 20; j++)
        {
            for (int i = 0; i < 20; i++)
            {
                int hi = data.at(bI++);
                int lo = data.at(bI++);
                if (hi < 0 || lo < 0)
                    icon[j * 20 + i] = bgColor;
                else
                    icon[j * 20 + i] = (uint16_t)((hi << 8) | lo);
            }
        }
        hasIcon = true;

        // Rundung sofort einfügen → überschreibt Ecken mit bgColor
        applyRoundMask(bgColor, radius);
        return true;
    }

    void applyRoundMask(uint16_t bgColor = PRIMARY, uint8_t radius = 5)
    {
        if (!hasIcon)
            return;
        if (radius > 10)
            radius = 10;

        for (int j = 0; j < 20; ++j)
        {
            for (int i = 0; i < 20; ++i)
            {
                bool pixelVisible = true;

                // obere linke Ecke
                if (i < radius && j < radius)
                {
                    int dx = radius - 1 - i;
                    int dy = radius - 1 - j;
                    if (dx * dx + dy * dy >= radius * radius)
                        pixelVisible = false;
                }
                // obere rechte Ecke
                else if (i >= 20 - radius && j < radius)
                {
                    int dx = i - (20 - radius);
                    int dy = radius - 1 - j;
                    if (dx * dx + dy * dy >= radius * radius)
                        pixelVisible = false;
                }
                // untere linke Ecke
                else if (i < radius && j >= 20 - radius)
                {
                    int dx = radius - 1 - i;
                    int dy = j - (20 - radius);
                    if (dx * dx + dy * dy >= radius * radius)
                        pixelVisible = false;
                }
                // untere rechte Ecke
                else if (i >= 20 - radius && j >= 20 - radius)
                {
                    int dx = i - (20 - radius);
                    int dy = j - (20 - radius);
                    if (dx * dx + dy * dy >= radius * radius)
                        pixelVisible = false;
                }

                if (!pixelVisible)
                    icon[j * 20 + i] = bgColor;
            }
        }
    }

    void drawIcon(int x, int y)
    {
        if (hasIcon)
            Screen::tft.pushImage(x, y, 20, 20, icon);
    }

    bool operator==(const AppRenderData &o) const
    {
        return path == o.path && name == o.name;
    }
};

unsigned long menuUpdateTime = 0;

#define SCROLL_OFF_MENU_START 20

void Windows::drawMenu(Vec pos, Vec move, MouseState state)
{
    using Screen::tft;
    static int scrollYOff = SCROLL_OFF_MENU_START;
    int itemHeight = 30; // mehr Platz, damit Icon+Text passen
    int itemWidth = 250;

    Rect screen = {{0, 0}, {320, 240}};
    Rect topSelect = {{10, 10}, {300, 60}};
    Rect programsView = {{10, topSelect.pos.y + topSelect.dimensions.y},
                         {300, screen.dimensions.y - topSelect.dimensions.y - topSelect.pos.y}};

    // Persistente App-Liste
    static std::vector<AppRenderData> apps;
    static std::vector<ENC_FS::Path> lastPaths; // für Änderungserkennung
    static unsigned long lastMenuRender = 0;

    bool appsChanged = false;
    bool needRedraw = false;

    // --- Periodisch Verzeichnis prüfen (nur Pfade vergleichen) ---
    if (menuUpdateTime == 0 || millis() - menuUpdateTime > 10000)
    {
        menuUpdateTime = millis();
        auto entries = ENC_FS::readDir({"programs"});

        std::vector<ENC_FS::Path> newPaths;
        for (String &e : entries)
            newPaths.push_back({"programs", e});

        if (newPaths != lastPaths) // Änderung erkannt
        {
            apps.clear();
            for (auto &p : newPaths)
            {
                apps.emplace_back();
                AppRenderData &app = apps.back();
                app.path = p;
                auto namePath = app.path;
                namePath.push_back("name.txt");
                app.name = ENC_FS::readFileString(namePath);
                Serial.println(">>> namePath: " + ENC_FS::path2Str(namePath));
                auto iconPath = app.path;
                iconPath.push_back("icon-20x20.raw");
                Serial.println(app.loadIcon(iconPath) ? "Icon Loaded" : "Icon Load Failed");
            }
            lastPaths = newPaths;
            appsChanged = true;
        }
    }

    // --- Scrollverhalten ---
    if (programsView.isIn(pos) && state == MouseState::Held)
    {
        int newScroll = max(SCROLL_OFF_MENU_START, scrollYOff + move.y);
        if (newScroll != scrollYOff)
        {
            scrollYOff = newScroll;
            needRedraw = true;
        }
    }

    if (lastMenuRender == 0)
        needRedraw = true;

    if (appsChanged)
        needRedraw = true;

    if (millis() - lastMenuRender > 10000)
        needRedraw = true;

    if (!needRedraw)
    {
        // Klick → App starten
        if (state == MouseState::Down)
        {
            int i = 0;
            for (auto &app : apps)
            {
                i++;
                Rect appRect = {{10, (scrollYOff + (i + 1) * itemHeight)},
                                {itemWidth, itemHeight}};
                if (!appRect.intersects(programsView))
                    continue;

                if (programsView.isIn(pos))
                {
                    if (appRect.isIn(pos))
                    {
                        executeApplication({ENC_FS::path2Str(app.path)});
                        Windows::isRendering = true;
                        break;
                    }
                }
            }
        }
        return;
    }

    // --- Render ---
    tft.fillScreen(BG);
    tft.fillRoundRect(topSelect.pos.x, topSelect.pos.y,
                      topSelect.dimensions.x, topSelect.dimensions.y, 5,
                      PRIMARY);

    tft.setTextSize(2);

    tft.setViewport(programsView.pos.x, programsView.pos.y, programsView.dimensions.x, programsView.dimensions.y, false);

    int i = 0;
    for (auto &app : apps)
    {
        i++;
        Rect appRect = {{10, (scrollYOff + (i + 1) * itemHeight)},
                        {itemWidth, itemHeight}};

        if (!appRect.intersects(programsView))
            continue;

        tft.fillRoundRect(appRect.pos.x, appRect.pos.y,
                          appRect.dimensions.x, appRect.dimensions.y - 5,
                          5, PRIMARY);

        // Icon zeichnen
        if (app.hasIcon)
        {
            app.drawIcon(appRect.pos.x + 5, appRect.pos.y + 3);
        }
        else
        {
            // Platzhalter
            tft.fillRoundRect(appRect.pos.x + 5, appRect.pos.y + 5,
                              20, 20, 5, PH);
        }

        // Name daneben
        tft.setCursor(appRect.pos.x + 30, appRect.pos.y + 5);
        tft.print(app.name);

        if (programsView.isIn(pos))
        {
            if (appRect.isIn(pos))
            {
                executeApplication({ENC_FS::path2Str(app.path)});
                Windows::isRendering = true;
                break;
            }
        }
    }

    Screen::tft.resetViewport();

    drawTime();
    lastMenuRender = millis();
    delay(10);
}
