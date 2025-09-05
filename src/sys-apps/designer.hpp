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
    vector<uint16_t> colors; // relevant for custom, saved even for light/dark
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
    for (auto c : t.colors)
    {
        String part = String(c, HEX);
        while (part.length() < 4)
            part = "0" + part;
        s += ":" + part;
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
    return t;
}

// ---------- Storage ----------
static inline void saveTheme(const Theme &t)
{
    String content = themeToString(t);
    Serial.println("Saving theme: " + content);

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

    Serial.println("Theme file exists: " + String(fileExists));
    Serial.println("Theme file content: " + content);

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

    // For light/dark, fill missing colors with defaults
    if (t.mode == "light")
    {
        Theme def = defaultLight();
        for (size_t i = 0; i < def.colors.size(); i++)
        {
            if (i >= t.colors.size())
                t.colors.push_back(def.colors[i]);
        }
    }
    else if (t.mode == "dark")
    {
        Theme def = defaultDark();
        for (size_t i = 0; i < def.colors.size(); i++)
        {
            if (i >= t.colors.size())
                t.colors.push_back(def.colors[i]);
        }
    }

    Serial.println("Loaded " + t.mode + " theme");
    return t;
}

// ---------- Apply ----------
static inline void applyTheme(const Theme &t)
{
    Serial.println("Applying theme: " + t.mode);
    if (t.colors.size() < 10)
    {
        Serial.println("Warning: theme colors < 10, skipping apply!");
        return;
    }

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

    for (size_t i = 0; i < 10; i++)
        Serial.printf("  color[%d] = 0x%04x\n", i, t.colors[i]);
}

static inline void applyColorPalette()
{
    Theme t = loadTheme();
    applyTheme(t);
}

// Converts RGB888 to RGB565
static inline uint16_t rgbTo565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Simple color picker: maps x/y in a fixed rectangle to RGB565
// x,y = touch coordinates
// rect = area of the color picker on screen
static inline uint16_t pickColor(int x, int y, int rectX = 20, int rectY = 80, int width = 300, int height = 50)
{
    TFT_eSPI &tft = Screen::tft;

    // Clamp x/y inside rect
    if (x < rectX)
        x = rectX;
    if (x > rectX + width)
        x = rectX + width;
    if (y < rectY)
        y = rectY;
    if (y > rectY + height)
        y = rectY + height;

    // Map x to hue (0..360)
    float hue = float(x - rectX) / float(width) * 360.0f;

    // Full saturation and value
    float s = 1.0f;
    float v = 1.0f;

    // HSV -> RGB
    int hi = int(hue / 60.0f) % 6;
    float f = (hue / 60.0f) - hi;
    float p = v * (1 - s);
    float q = v * (1 - f * s);
    float t = v * (1 - (1 - f) * s);

    float r = 0, g = 0, b = 0;
    switch (hi)
    {
    case 0:
        r = v;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = v;
        b = p;
        break;
    case 2:
        r = p;
        g = v;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = v;
        break;
    case 4:
        r = t;
        g = p;
        b = v;
        break;
    case 5:
        r = v;
        g = p;
        b = q;
        break;
    }

    // Convert float 0..1 to 0..255
    uint8_t R = uint8_t(r * 255);
    uint8_t G = uint8_t(g * 255);
    uint8_t B = uint8_t(b * 255);

    // Optionally draw a preview rectangle
    tft.fillRect(rectX, rectY, width, height, rgbTo565(R, G, B));

    return rgbTo565(R, G, B);
}

// ---------- Designer ----------
static inline void openDesigner()
{
    TFT_eSPI &tft = Screen::tft;
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
        if (mode.equalsIgnoreCase("custom"))
        {
            x = 20 - scrollOffset;
            int y = topMargin;
            for (size_t i = 0; i < working.colors.size(); i++)
            {
                tft.setTextDatum(TC_DATUM);
                tft.setTextColor(Style::Colors::text, Style::Colors::bg);
                tft.drawString(colorNames[i], x + colorBoxSize / 2, y);

                tft.fillRoundRect(x, y + 20, colorBoxSize, colorBoxSize, 5, working.colors[i]);
                tft.drawRoundRect(x, y + 20, colorBoxSize, colorBoxSize, 5, Style::Colors::text);

                uint16_t c = working.colors[i];
                uint8_t r = ((c >> 11) & 0x1F) << 3;
                uint8_t g = ((c >> 5) & 0x3F) << 2;
                uint8_t b = (c & 0x1F) << 3;
                tft.setTextDatum(TC_DATUM);
                tft.setTextColor(Style::Colors::text, Style::Colors::bg);
                tft.drawString(String(r) + "," + String(g) + "," + String(b), x + colorBoxSize / 2, y + colorBoxSize + 35);

                x += colorBoxSize + spacing;
            }
        }
    };

    drawUI();

    while (running)
    {
        auto evt = Screen::getTouchPos();
        if (!evt.clicked)
            continue;

        // Mode buttons
        if (evt.y >= 40 && evt.y <= 40 + buttonHeight)
        {
            if (evt.x >= 20 && evt.x <= 100)
                mode = "light";
            else if (evt.x >= 110 && evt.x <= 190)
                mode = "dark";
            else if (evt.x >= 200 && evt.x <= 280)
                mode = "custom";

            working.mode = mode;
            drawUI();
            continue;
        }

        // Custom colors touch
        if (mode.equalsIgnoreCase("custom"))
        {
            int xStart = 20 - scrollOffset;
            int y = topMargin;
            for (size_t i = 0; i < working.colors.size(); i++)
            {
                int x0 = xStart + i * (colorBoxSize + spacing);
                int y0 = y + 20;
                if (evt.x >= x0 && evt.x <= x0 + colorBoxSize &&
                    evt.y >= y0 && evt.y <= y0 + colorBoxSize)
                {
                    working.colors[i] = pickColor(evt.x, evt.y);
                    drawUI();
                    break;
                }
            }
        }

        // Exit button (example)
        if (evt.y > tft.height() - 40)
        {
            running = false;
        }
    }

    // Apply final theme once
    current = working;
    saveTheme(current);
    applyTheme(current);
    Serial.println("Designer exited, theme applied.");
}
