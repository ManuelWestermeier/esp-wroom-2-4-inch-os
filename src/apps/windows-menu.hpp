#include "windows.hpp" // for Vec, MouseState, etc.

#include <Arduino.h>
#include <vector>

#include "../sys-apps/designer.hpp"
#include "../sys-apps/file-picker.hpp"

#include "../icons/index.hpp"

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

struct ShortCut
{
    String name;
    String svg;
};

unsigned long menuUpdateTime = 0;

#define SCROLL_OFF_Y_MENU_START 20

std::vector<ShortCut> shortCuts = {
    {"Settings", SVG::settings},
    {"Account", SVG::account},
    {"Design", SVG::folder},
    {"WiFi", SVG::wifi},
    {"Folders", SVG::design},
};

void Windows::drawMenu(Vec pos, Vec move, MouseState state)
{
    using Screen::tft;
    static int scrollYOff = SCROLL_OFF_Y_MENU_START;
    static int scrollXOff = 0;
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
    static unsigned long lastMenuRenderCall = 0;

    bool appsChanged = false;
    bool needRedraw = false;
    bool topRedraw = false;
    bool bottomRedraw = false;

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

    if (lastMenuRender == 0 || lastMenuRenderCall == 0)
        needRedraw = true;

    if (appsChanged)
        needRedraw = true;

    if (millis() - lastMenuRenderCall > 300)
    {
        needRedraw = true;
    }
    lastMenuRenderCall = millis();

    if (needRedraw)
    {
        topRedraw = true;
        bottomRedraw = true;
        tft.fillScreen(BG);
    }

    // --- Scrollverhalten ---
    if (programsView.isIn(pos) && state == MouseState::Held)
    {
        int newScroll = min(SCROLL_OFF_Y_MENU_START, scrollYOff + move.y);
        if (newScroll != scrollYOff)
        {
            scrollYOff = newScroll;
            needRedraw = true;
            bottomRedraw = true;
        }
    }

    // --- Scrollverhalten ---
    if (topSelect.isIn(pos) && state == MouseState::Held)
    {
        int newScroll = min(0, scrollXOff + move.x);
        if (newScroll != scrollXOff)
        {
            scrollXOff = newScroll;
            needRedraw = true;
            topRedraw = true;
        }
    }

    // Klick → App starten
    if (state == MouseState::Down)
    {
        int i = 0;
        if (programsView.isIn(pos))
        {
            for (auto &app : apps)
            {
                i++;
                Rect appRect = {{10, (scrollYOff + (i + 1) * itemHeight)},
                                {itemWidth, itemHeight}};
                if (!appRect.intersects(programsView))
                    continue;

                if (appRect.isIn(pos))
                {
                    executeApplication({ENC_FS::path2Str(app.path)});
                    Windows::isRendering = true;
                    break;
                }
            }
        }
        else if (topSelect.isIn(pos))
        {
            int scXPos = topSelect.pos.x + 5 + scrollXOff;
            for (const auto &shortCut : shortCuts)
            {
                int h = topSelect.dimensions.y - 10;
                int w = max(h, (int)(shortCut.name.length() * 6 + 10));
                Rect scPos = {{scXPos, topSelect.pos.y + 5}, {w, h}};

                if (scPos.isIn(pos))
                {
                    if (shortCut.name == "Settings")
                    {
                    }
                    else if (shortCut.name == "Account")
                    {
                    }
                    else if (shortCut.name == "Design")
                    {
                        return openDesigner();
                    }
                    else if (shortCut.name == "WiFi")
                    {
                    }
                    else if (shortCut.name == "Folders")
                    {
                        filePicker();
                        return;
                    }
                    break;
                }

                scXPos += w + 5;
            }
        }
    }

    if (!needRedraw)
        return;

    // --- Render ---
    if (topRedraw)
    {
        tft.fillRoundRect(topSelect.pos.x, topSelect.pos.y,
                          topSelect.dimensions.x, topSelect.dimensions.y, 5,
                          PRIMARY);

        tft.setViewport(topSelect.pos.x, topSelect.pos.y + 5, topSelect.dimensions.x, topSelect.dimensions.y - 5, false);

        int scXPos = topSelect.pos.x + 5 + scrollXOff;
        for (const auto &shortCut : shortCuts)
        {
            int h = topSelect.dimensions.y - 10;
            int w = max(h, (int)(shortCut.name.length() * 6 + 10));
            Rect scPos = {{scXPos, topSelect.pos.y + 5}, {w, h}};

            tft.fillRoundRect(scPos.pos.x, scPos.pos.y, scPos.dimensions.x, scPos.dimensions.y, 3, BG); // BG

            tft.drawString(shortCut.name, scPos.pos.x + 5, scPos.pos.y + 5, 1);

            if (!shortCut.svg.isEmpty())
            {
                ESP32_SVG svg(&tft);
                int d = h - 20;
                svg.drawString(shortCut.svg, scPos.pos.x + ((w / 2) - (d / 2)), scPos.pos.y + 15, d, d, TEXT);
            }

            scXPos += w + 5;
        }

        Screen::tft.resetViewport();
    }
    if (bottomRedraw)
    {
        tft.setTextSize(2);

        // y padding 10 => not colliding with the top bar
        tft.setViewport(programsView.pos.x, programsView.pos.y + 10, programsView.dimensions.x, programsView.dimensions.y, false);
        tft.fillRect(programsView.pos.x, programsView.pos.y + 10, programsView.dimensions.x, programsView.dimensions.y, BG);

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
        }

        Screen::tft.resetViewport();
    }

    drawTime();
    lastMenuRender = millis();
    delay(10);
}
