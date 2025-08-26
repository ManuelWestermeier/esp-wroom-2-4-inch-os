#include "windows.hpp" // for Vec, MouseState, etc.
#include <vector>
#include <SD.h>

extern void executeApplication(const std::vector<String> &args);

struct AppRenderData
{
    String name;
    String path;
    uint16_t icon[20 * 20];         // Roh-Icon (falls geladen)
    uint16_t roundedIcon[20 * 20];  // Vorbereitetes, abgerundetes Icon (kein Stack)
    bool hasIcon = false;
    bool roundedReady = false;

    bool loadIcon(const String &filename, uint16_t bgColor = RGB(255, 240, 255), uint8_t radius = 5)
    {
        File f = SD.open(filename, FILE_READ);
        if (!f)
            return false;

        // defensive: prüfen, ob mindestens 4 bytes für w/h da sind
        int hb = f.available();
        if (hb < 4)
        {
            f.close();
            return false;
        }

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
                // defensive: falls Datei kürzer ist, f.read() liefert -1 -> wir behandeln als bgColor
                int hi = f.read();
                int lo = f.read();
                if (hi < 0 || lo < 0)
                    icon[j * 20 + i] = bgColor;
                else
                    icon[j * 20 + i] = (uint16_t)((hi << 8) | lo);
            }
        }
        f.close();
        hasIcon = true;

        // Bereite das gerundete Icon sofort vor (kein Stack-Buffer nötig)
        prepareRounded(bgColor, radius);
        return true;
    }

    // Bereitet roundedIcon vor und setzt roundedReady
    void prepareRounded(uint16_t bgColor = RGB(255, 240, 255), uint8_t radius = 5)
    {
        // radius darf nicht größer als 10 sein (20/2)
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

                if (pixelVisible && hasIcon)
                    roundedIcon[j * 20 + i] = icon[j * 20 + i];
                else
                    roundedIcon[j * 20 + i] = bgColor;
            }
        }
        roundedReady = true;
    }

    // Zeichnet das bereits vorbereitete roundedIcon (keine großen Stack-Arrays)
    void drawIconRounded(int x, int y)
    {
        if (roundedReady)
        {
            Screen::tft.pushImage(x, y, 20, 20, roundedIcon);
        }
        else if (hasIcon)
        {
            // Fallback: falls prepareRounded nicht gelaufen ist (sollte selten sein)
            prepareRounded();
            Screen::tft.pushImage(x, y, 20, 20, roundedIcon);
        }
        else
        {
            // Nichts (oder caller zeichnet Platzhalter)
        }
    }

    bool operator==(const AppRenderData &o) const
    {
        return path == o.path && name == o.name;
    }
};

unsigned long menuUpdateTime = 0;

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

    // Persistente App-Liste
    static std::vector<AppRenderData> apps;
    static std::vector<String> lastPaths; // für Änderungserkennung
    static unsigned long lastMenuRender = 0;

    bool appsChanged = false;
    bool needRedraw = false;

    // --- Periodisch Verzeichnis prüfen (nur Pfade vergleichen) ---
    if (menuUpdateTime == 0 || millis() - menuUpdateTime > 10000)
    {
        menuUpdateTime = millis();
        auto entries = SD_FS::readDir("/public/programs");
        std::vector<String> newPaths;
        for (auto &e : entries)
            newPaths.push_back(e.path());

        if (newPaths != lastPaths) // Änderung erkannt
        {
            apps.clear();
            // Direkt in den Vector emplace_back, damit kein großes lokales Objekt auf dem Stack liegt
            for (auto &p : newPaths)
            {
                apps.emplace_back();                   // erstellt ein neues AppRenderData im Heap-Speicher des Vectors
                AppRenderData &app = apps.back();      // Referenz auf das gerade eingefügte Element
                app.path = p;
                app.name = SD_FS::readFile(app.path + "/name.txt");
                // Versuche Icon zu laden (prepareRounded wird dort aufgerufen)
                app.loadIcon(app.path + "/icon-20x20.raw");
            }
            lastPaths = newPaths;
            appsChanged = true;
        }
    }

    // --- Scrollverhalten (sofort anwenden, sorgt für redraw) ---
    if (programsView.isIn(pos) && state == MouseState::Held)
    {
        int newScroll = max(0, scrollYOff + move.y);
        if (newScroll != scrollYOff)
        {
            scrollYOff = newScroll;
            needRedraw = true;
        }
    }

    // Wenn Anzahl Apps beim ersten Mal 0 ist, erzwinge Render
    if (lastMenuRender == 0)
        needRedraw = true;

    // Wenn Apps geändert => redraw
    if (appsChanged)
        needRedraw = true;

    // Aktualisiere Time-Display spätestens jede 1s (sonst kein redraw)
    if (millis() - lastMenuRender > 1000)
        needRedraw = true;

    // Falls nichts zu tun: nur Klicks behandeln (App-Start) und Rückkehren
    if (!needRedraw)
    {
        // Klick → App starten (auch wenn wir gerade nicht redrawen)
        if (state == MouseState::Down)
        {
            int i = 0;
            for (auto &app : apps)
            {
                i++;
                Rect appRect = {{10, scrollYOff + i * itemHeight},
                                {itemWidth, itemHeight}};
                if (appRect.isIn(pos))
                {
                    executeApplication({app.path + "/", "Arg1", "Hi"});
                    Windows::isRendering = true;
                    break;
                }
            }
        }
        return;
    }

    // --- Eigentliche Render-Phase (nur wenn needRedraw) ---
    // Hintergrund oben (Header)
    tft.fillRect(topSelect.pos.x, topSelect.pos.y,
                 topSelect.dimensions.x, topSelect.dimensions.y,
                 RGB(255, 240, 255));

    tft.setTextSize(2);

    // Liste zeichnen
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

        // Hintergrund für App-Eintrag (rounded)
        tft.fillRoundRect(appRect.pos.x, appRect.pos.y,
                          appRect.dimensions.x, appRect.dimensions.y - 5,
                          5, RGB(255, 240, 255));

        // Icon links zeichnen (mit vorgefertigtem Rounded-Icon)
        if (app.hasIcon && app.roundedReady)
        {
            app.drawIconRounded(appRect.pos.x + 5, appRect.pos.y + 5);
        }
        else
        {
            // Platzhalter: abgerundetes Kästchen
            tft.fillRoundRect(appRect.pos.x + 5, appRect.pos.y + 5, 20, 20, 5, RGB(200, 200, 200));
        }

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

    // Zeit/Status (wird bei Bedarf jede Sekunde neu gezeichnet)
    drawTime();

    // letzte Render-Zustände updaten
    lastMenuRender = millis();

    // kleine Pause wie vorher (optional)
    delay(10);
}
