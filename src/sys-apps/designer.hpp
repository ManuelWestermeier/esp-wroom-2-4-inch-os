#pragma once

#include <Arduino.h>
#include <vector>

#include "../screen/index.hpp"
#include "../styles/global.hpp"
#include "../fs/index.hpp"
#include "../fs/enc-fs.hpp"
#include "../auth/auth.hpp"

using std::vector;

struct Theme
{
    String mode;             // "light", "dark", "custom"
    vector<uint16_t> colors; // Nur bei custom relevant
};

// ---------- Defaults ----------
static inline Theme defaultDark()
{
    return {"dark", {
                        RGB(18, 18, 28),    // bg
                        RGB(230, 230, 240), // text
                        RGB(28, 28, 40),    // primary
                        RGB(100, 200, 255), // accent
                        RGB(70, 150, 255),  // accent2
                        RGB(50, 120, 220),  // accent3
                        RGB(255, 100, 100), // danger
                        RGB(40, 100, 180),  // pressed
                        RGB(120, 120, 140), // placeholder
                        RGB(255, 255, 255)  // accentText
                    }};
}

static inline Theme defaultLight()
{
    return {"light", {
                         RGB(245, 245, 255), // bg
                         RGB(2, 2, 4),       // text
                         RGB(255, 240, 255), // primary
                         RGB(30, 144, 255),  // accent
                         RGB(220, 220, 250), // accent2
                         RGB(180, 180, 255), // accent3
                         RGB(255, 150, 150), // danger
                         RGB(30, 144, 255),  // pressed
                         RGB(200, 200, 200), // placeholder
                         TFT_WHITE           // accentText
                     }};
}

// ---------- Helpers ----------
static inline String themeToString(const Theme &t)
{
    String s = t.mode;
    if (t.mode == "custom")
    {
        for (auto c : t.colors)
        {
            s += ":" + String(c, HEX);
        }
    }
    return s;
}

static inline Theme stringToTheme(const String &s)
{
    int firstColon = s.indexOf(':');
    if (firstColon == -1)
        return {s, {}};

    String mode = s.substring(0, firstColon);
    Theme t{mode, {}};

    if (mode == "custom")
    {
        int start = firstColon + 1;
        while (start < s.length())
        {
            int pos = s.indexOf(':', start);
            String part = (pos == -1) ? s.substring(start) : s.substring(start, pos);
            t.colors.push_back((uint16_t)strtol(part.c_str(), nullptr, 16));
            if (pos == -1)
                break;
            start = pos + 1;
        }
    }
    return t;
}

// ---------- Storage ----------
static inline void saveTheme(const Theme &t)
{
    String content = themeToString(t);

    if (Auth::username.isEmpty())
    {
        SD_FS::writeFile("/theme.txt", content);
    }
    else
    {
        ENC_FS::writeFileString(ENC_FS::str2Path("/theme.txt"), content);
    }
}

static inline Theme loadTheme()
{
    bool fileExists = false;
    String content;

    if (Auth::username.isEmpty())
    {
        fileExists = SD_FS::exists("/theme.txt");
        if (fileExists)
        {
            content = SD_FS::readFile("/theme.txt");
        }
    }
    else
    {
        auto path = ENC_FS::str2Path("/theme.txt");
        fileExists = ENC_FS::exists(path);
        if (fileExists)
        {
            content = ENC_FS::readFileString(path);
        }
    }

    // Datei existiert nicht → Default setzen und abspeichern
    if (!fileExists || content.isEmpty())
    {
#ifdef DARKMODE
        Theme def = defaultDark();
#else
        Theme def = defaultLight();
#endif
        saveTheme(def); // direkt eine gültige Datei erzeugen
        return def;
    }

    Theme t = stringToTheme(content);
    if (t.mode == "light")
        return defaultLight();
    if (t.mode == "dark")
        return defaultDark();
    return t; // custom
}

// ---------- Apply ----------
static inline void applyColorPalette()
{
    Theme t = loadTheme();

    // Nur wenn genug Farben da sind (bei custom)
    if (t.mode == "custom" && t.colors.size() >= 10)
    {
        Style::Colors::bg = t.colors[0];
        Style::Colors::text = t.colors[1];
        Style::Colors::primary = t.colors[2];
        Style::Colors::accent = t.colors[3];
        Style::Colors::accent2 = t.colors[4];
        Style::Colors::accent3 = t.colors[5];
        Style::Colors::danger = t.colors[6];
        Style::Colors::pressed = t.colors[7];
        Style::Colors::placeholder = t.colors[8];
        Style::Colors::accentText = t.colors[9];
    }
    else if (t.mode == "dark")
    {
        Theme d = defaultDark();
        applyColorPalette(); // oder direkt Werte aus d.colors übernehmen
    }
    else if (t.mode == "light")
    {
        Theme l = defaultLight();
        applyColorPalette(); // dito
    }
}

static inline void openDesigner()
{
    TFT_eSPI &tft = Screen::tft; // angenommen dein globaler TFT handler
    tft.fillScreen(BG);

    Theme current = loadTheme();
    String mode = current.mode; // "light", "dark", "custom"
    Theme working = current;    // veränderbare Kopie

    bool running = true;

    auto drawUI = [&]()
    {
        tft.fillScreen(BG);

        // Titel
        tft.setTextColor(TEXT, BG);
        tft.setTextDatum(TC_DATUM);
        tft.drawString("Theme Designer", tft.width() / 2, 10);

        // Mode toggle
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Mode: " + mode, tft.width() / 2, 40);

        // Buttons
        tft.fillRoundRect(20, tft.height() - 50, 80, 30, 5, ACCENT);
        tft.setTextColor(AT);
        tft.drawString("OK", 60, tft.height() - 35);

        tft.fillRoundRect(tft.width() - 100, tft.height() - 50, 80, 30, 5, DANGER);
        tft.setTextColor(AT);
        tft.drawString("Cancel", tft.width() - 60, tft.height() - 35);

        // Nur bei custom: Farbkästen
        if (mode == "custom")
        {
            int x = 20, y = 80;
            for (size_t i = 0; i < working.colors.size(); i++)
            {
                tft.fillRect(x, y, 40, 40, working.colors[i]);
                tft.drawRect(x, y, 40, 40, TEXT);
                x += 50;
                if (x + 40 > tft.width())
                {
                    x = 20;
                    y += 50;
                }
            }
        }
    };

    drawUI();

    while (running)
    {
        Screen::TouchPos tp = Screen::getTouchPos();

        if (tp.clicked)
        {
            int tx = tp.x, ty = tp.y;

            // Mode toggle area
            if (ty > 20 && ty < 60)
            {
                if (mode == "light")
                    mode = "dark";
                else if (mode == "dark")
                    mode = "custom";
                else
                    mode = "light";
                working = (mode == "light") ? defaultLight() : (mode == "dark") ? defaultDark()
                                                                                : current;
                drawUI();
            }

            // OK
            if (tx > 20 && tx < 100 && ty > tft.height() - 50)
            {
                working.mode = mode;
                saveTheme(working);
                applyColorPalette();
                running = false;
            }

            // Cancel
            if (tx > tft.width() - 100 && ty > tft.height() - 50)
            {
                running = false;
            }

            // Custom-Farbkästen
            if (mode == "custom")
            {
                int x = 20, y = 80;
                for (size_t i = 0; i < working.colors.size(); i++)
                {
                    if (tx >= x && tx < x + 40 && ty >= y && ty < y + 40)
                    {
                        // einfacher Farbwechsel (cycling demo)
                        uint16_t c = working.colors[i];
                        uint8_t r = (c >> 11) & 0x1F;
                        uint8_t g = (c >> 5) & 0x3F;
                        uint8_t b = c & 0x1F;
                        r = (r + 5) % 32;
                        g = (g + 10) % 64;
                        b = (b + 5) % 32;
                        working.colors[i] = (r << 11) | (g << 5) | b;
                        drawUI();
                        break;
                    }
                    x += 50;
                    if (x + 40 > tft.width())
                    {
                        x = 20;
                        y += 50;
                    }
                }
            }
        }

        delay(50); // entprellen
    }

    tft.fillScreen(BG); // zurück zur UI
}
