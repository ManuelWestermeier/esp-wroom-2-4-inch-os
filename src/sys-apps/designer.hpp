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
    vector<uint16_t> colors; // nur bei custom relevant
};

// ---------- Defaults ----------
static inline Theme defaultDark()
{
    static const uint16_t colors[] = {
        RGB(18, 18, 28), RGB(230, 230, 240), RGB(28, 28, 40), RGB(100, 200, 255),
        RGB(70, 150, 255), RGB(50, 120, 220), RGB(255, 100, 100), RGB(40, 100, 180),
        RGB(120, 120, 140), RGB(255, 255, 255)};
    return {"dark", vector<uint16_t>(colors, colors + 10)};
}

static inline Theme defaultLight()
{
    static const uint16_t colors[] = {
        RGB(245, 245, 255), RGB(2, 2, 4), RGB(255, 240, 255), RGB(30, 144, 255),
        RGB(220, 220, 250), RGB(180, 180, 255), RGB(255, 150, 150), RGB(30, 144, 255),
        RGB(200, 200, 200), RGB(255, 255, 255)};
    return {"light", vector<uint16_t>(colors, colors + 10)};
}

// ---------- Helpers ----------
static inline String themeToString(const Theme &t)
{
    String s = t.mode;
    if (t.mode == "custom")
    {
        for (auto c : t.colors)
        {
            String part = String(c, HEX);
            while (part.length() < 4)
                part = "0" + part;
            s += ":" + part;
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
            char *endptr;
            long val = strtol(part.c_str(), &endptr, 16);
            if (*endptr == 0)
                t.colors.push_back((uint16_t)val);
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
    if (!Auth::username.isEmpty())
        ENC_FS::writeFileString(ENC_FS::str2Path("/theme.txt"), content);
    SD_FS::writeFile("/theme.txt", content);
}

static inline Theme loadTheme()
{
    bool fileExists = false;
    String content;

    if (Auth::username.isEmpty()) // not authenticated
    {
        fileExists = SD_FS::exists("/theme.txt");
        if (fileExists)
            content = SD_FS::readFile("/theme.txt");
    }
    else
    {
        auto path = ENC_FS::str2Path("/theme.txt");
        fileExists = ENC_FS::exists(path);
        if (fileExists)
            content = ENC_FS::readFileString(path);
    }

    if (!fileExists || content.isEmpty())
    {
#ifdef DARKMODE
        Theme def = defaultDark();
#else
        Theme def = defaultLight();
#endif
        saveTheme(def);
        return def;
    }

    Theme t = stringToTheme(content);
    if (t.mode == "light")
        return defaultLight();
    if (t.mode == "dark")
        return defaultDark();
    return t;
}

// ---------- Apply ----------
static inline void applyTheme(const Theme &t)
{
    const Theme *th = &t;

    // Stack-sichere statische Defaults
    static Theme dark = defaultDark();
    static Theme light = defaultLight();

    if (t.mode == "dark")
        th = &dark;
    else if (t.mode == "light")
        th = &light;

    if (th->colors.size() >= 10)
    {
        Style::Colors::bg = th->colors[0];
        Style::Colors::text = th->colors[1];
        Style::Colors::primary = th->colors[2];
        Style::Colors::accent = th->colors[3];
        Style::Colors::accent2 = th->colors[4];
        Style::Colors::accent3 = th->colors[5];
        Style::Colors::danger = th->colors[6];
        Style::Colors::pressed = th->colors[7];
        Style::Colors::placeholder = th->colors[8];
        Style::Colors::accentText = th->colors[9];
    }
}

static inline void applyColorPalette()
{
    Theme t = loadTheme();
    applyTheme(t);
}

// ---------- Designer ----------
static inline void openDesigner()
{
    TFT_eSPI &tft = Screen::tft;
    tft.fillScreen(Style::Colors::bg);

    Theme current = loadTheme();
    Theme working = current;
    String mode = current.mode;
    bool running = true;
    int scrollOffset = 0;

    const int buttonHeight = 30;
    const int colorBoxSize = 40;
    const int spacing = 20;
    const int topMargin = 80;

    const char *colorNames[] = {
        "Background", "Text", "Primary", "Accent", "Accent2",
        "Accent3", "Danger", "Pressed", "Placeholder", "AccentText"};

    auto drawUI = [&]()
    {
        tft.fillScreen(Style::Colors::bg);

        // Title
        tft.setTextColor(Style::Colors::text, Style::Colors::bg);
        tft.setTextDatum(TC_DATUM);
        tft.setTextSize(2);
        tft.drawString("Theme Designer", tft.width() / 2, 10);
        tft.setTextSize(1);

        // Mode buttons
        int x = 20;
        const char *modes[] = {"Light", "Dark", "Custom"};
        for (auto &m : modes)
        {
            uint16_t bgCol = (mode.equalsIgnoreCase(m)) ? Style::Colors::accent : Style::Colors::primary;
            uint16_t textCol = (mode.equalsIgnoreCase(m)) ? Style::Colors::accentText : Style::Colors::text;

            tft.fillRoundRect(x, 40, 80, buttonHeight, 5, bgCol);
            tft.setTextColor(textCol, bgCol);
            tft.setTextDatum(MC_DATUM);
            tft.drawString(m, x + 40, 40 + buttonHeight / 2);
            x += 90;
        }

        // Custom colors
        if (mode == "custom")
        {
            x = 20 - scrollOffset;
            int y = topMargin;
            for (size_t i = 0; i < working.colors.size(); i++)
            {
                // Name
                tft.setTextDatum(TC_DATUM);
                tft.setTextColor(Style::Colors::text, Style::Colors::bg);
                tft.drawString(colorNames[i], x + colorBoxSize / 2, y);

                // Color box
                tft.fillRoundRect(x, y + 20, colorBoxSize, colorBoxSize, 5, working.colors[i]);
                tft.drawRoundRect(x, y + 20, colorBoxSize, colorBoxSize, 5, Style::Colors::text);

                // RGB text
                uint16_t c = working.colors[i];
                uint8_t r = ((c >> 11) & 0x1F) << 3;
                uint8_t g = ((c >> 5) & 0x3F) << 2;
                uint8_t b = (c & 0x1F) << 3;
                tft.setTextDatum(TC_DATUM);
                tft.setTextColor(Style::Colors::text, Style::Colors::bg);
                tft.drawString(String(r) + "," + String(g) + "," + String(b), x + colorBoxSize / 2, y + 65);

                x += colorBoxSize + spacing + 20;
            }
        }

        // OK & Cancel buttons
        tft.fillRoundRect(20, tft.height() - 50, 80, buttonHeight, 5, Style::Colors::accent);
        tft.setTextColor(Style::Colors::accentText);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("OK", 20 + 40, tft.height() - 50 + buttonHeight / 2);

        tft.fillRoundRect(tft.width() - 100, tft.height() - 50, 80, buttonHeight, 5, Style::Colors::danger);
        tft.setTextColor(Style::Colors::accentText);
        tft.drawString("Cancel", tft.width() - 100 + 40, tft.height() - 50 + buttonHeight / 2);
    };

    drawUI();

    int lastTouchX = -1;

    while (running)
    {
        Screen::TouchPos tp = Screen::getTouchPos();
        if (!tp.clicked)
        {
            lastTouchX = -1;
            delay(50);
            continue;
        }

        int tx = tp.x, ty = tp.y;

        // Mode buttons
        int mx = 20;
        const char *modes[] = {"Light", "Dark", "Custom"};
        for (auto &m : modes)
        {
            if (tx >= mx && tx <= mx + 80 && ty >= 40 && ty <= 40 + buttonHeight)
            {
                mode = m;
                working = (mode == "Light") ? defaultLight() : (mode == "Dark") ? defaultDark()
                                                                                : current;
                scrollOffset = 0;
                drawUI();
                break;
            }
            mx += 90;
        }

        // OK button
        if (tx >= 20 && tx <= 20 + 80 && ty >= tft.height() - 50 && ty <= tft.height() - 50 + buttonHeight)
        {
            working.mode = mode;
            working.mode.toLowerCase();
            saveTheme(working);
            applyTheme(working);
            running = false;
        }

        // Cancel button
        if (tx >= tft.width() - 100 && tx <= tft.width() - 20 && ty >= tft.height() - 50 && ty <= tft.height() - 50 + buttonHeight)
        {
            running = false;
        }

        // Custom color horizontal scroll
        if (mode == "Custom")
        {
            // Horizontal scroll
            if (lastTouchX >= 0 && ty >= topMargin && ty <= topMargin + 100)
            {
                int dx = tp.x - lastTouchX;
                scrollOffset -= dx;
                if (scrollOffset < 0)
                    scrollOffset = 0;
                int maxScroll = working.colors.size() * (colorBoxSize + spacing + 20) - tft.width() + 40;
                if (scrollOffset > maxScroll)
                    scrollOffset = maxScroll;
                drawUI();
            }
            lastTouchX = tp.x;

            // Tap color boxes
            int x = 20 - scrollOffset;
            int y = topMargin + 20;
            for (size_t i = 0; i < working.colors.size(); i++)
            {
                if (tx >= x && tx <= x + colorBoxSize && ty >= y && ty <= y + colorBoxSize)
                {
                    uint16_t c = working.colors[i];
                    uint8_t r = (c >> 11) & 0x1F;
                    uint8_t g = (c >> 5) & 0x3F;
                    uint8_t b = c & 0x1F;
                    r = (r + 1) % 32;
                    g = (g + 2) % 64;
                    b = (b + 1) % 32;
                    working.colors[i] = (r << 11) | (g << 5) | b;
                    drawUI();
                    break;
                }
                x += colorBoxSize + spacing + 20;
            }
        }

        delay(50);
    }

    tft.fillScreen(Style::Colors::bg);
}