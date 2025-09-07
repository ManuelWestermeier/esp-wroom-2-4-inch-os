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

// ---------- Fullscreen Color Picker ----------
// Returns the selected color (if OK) or the original color (if Cancel)
static inline uint16_t fullscreenColorPicker(uint16_t initialColor)
{
    TFT_eSPI &tft = Screen::tft;

    // Clear screen with bg (or black) while picking
    tft.fillScreen(TFT_BLACK);

    const int btnW = 100, btnH = 40;
    const int okX = 20, okY = 12;
    const int cancelX = tft.width() - btnW - 20, cancelY = 12;

    // Picker area starts below buttons, leaves a top margin
    const int pickerTop = okY + btnH + 8;
    const int pickerLeft = 0;
    const int pickerWidth = tft.width();
    const int pickerHeight = tft.height() - pickerTop - 20; // small bottom margin

    // initial selected
    uint16_t selected = initialColor;
    bool accepted = false;
    bool running = true;

    // Helper: convert HSV (h:0-360, s:0-1, v:0-1) -> RGB565
    auto hsvTo565 = [&](float h, float s, float v) -> uint16_t {
        float hh = h;
        if (hh >= 360.0f) hh = 0.0f;
        float f = (hh / 60.0f);
        int i = int(f);
        float frac = f - float(i);
        float p = v * (1.0f - s);
        float q = v * (1.0f - s * frac);
        float t = v * (1.0f - s * (1.0f - frac));
        float rr=0, gg=0, bb=0;
        switch (i)
        {
        case 0: rr = v; gg = t; bb = p; break;
        case 1: rr = q; gg = v; bb = p; break;
        case 2: rr = p; gg = v; bb = t; break;
        case 3: rr = p; gg = q; bb = v; break;
        case 4: rr = t; gg = p; bb = v; break;
        case 5: rr = v; gg = p; bb = q; break;
        default: rr = v; gg = p; bb = q; break;
        }
        return rgbTo565(uint8_t(rr * 255.0f), uint8_t(gg * 255.0f), uint8_t(bb * 255.0f));
    };

    // Draw the picker gradient: hue across X (0..360), saturation across Y (0..1), fixed V=1
    // We'll render column by column to keep memory footprint small.
    for (int x = 0; x < pickerWidth; x++)
    {
        float hue = float(x) / float(max(1, pickerWidth - 1)) * 360.0f;
        // For each column, draw vertical gradient for saturation (top = 0 => white-ish, bottom = 1 => full hue)
        for (int y = 0; y < pickerHeight; y++)
        {
            // y=0 => saturation = 0 (white), y=pickerHeight => saturation = 1 (full color)
            float sat = float(y) / float(max(1, pickerHeight - 1));
            // Because we want top to be desaturated (more white) and bottom full color, but keep V=1.
            uint16_t c = hsvTo565(hue, sat, 1.0f);
            tft.drawPixel(pickerLeft + x, pickerTop + y, c);
        }
    }

    // Draw OK and Cancel buttons
    tft.fillRoundRect(okX, okY, btnW, btnH, 6, Style::Colors::primary);
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(2);
    tft.setTextColor(Style::Colors::accentText, Style::Colors::primary);
    tft.drawString("OK", okX + btnW / 2, okY + btnH / 2);

    tft.fillRoundRect(cancelX, cancelY, btnW, btnH, 6, Style::Colors::danger);
    tft.setTextColor(Style::Colors::accentText, Style::Colors::danger);
    tft.drawString("Cancel", cancelX + btnW / 2, cancelY + btnH / 2);
    tft.setTextSize(1);

    // Initial preview circle (top center)
    auto drawPreview = [&](uint16_t col) {
        int cx = tft.width() / 2;
        int cy = okY + btnH / 2;
        int r = 16;
        tft.fillCircle(cx, cy, r + 2, Style::Colors::primary); // border
        tft.fillCircle(cx, cy, r, col);
    };

    drawPreview(selected);

    // Main pick loop
    while (running)
    {
        auto evt = Screen::getTouchPos();
        if (!evt.clicked)
        {
            delay(10);
            continue;
        }

        // Check OK
        if (evt.x >= okX && evt.x <= okX + btnW && evt.y >= okY && evt.y <= okY + btnH)
        {
            accepted = true;
            running = false;
            break;
        }

        // Check Cancel
        if (evt.x >= cancelX && evt.x <= cancelX + btnW && evt.y >= cancelY && evt.y <= cancelY + btnH)
        {
            accepted = false;
            running = false;
            break;
        }

        // If touched inside picker area -> compute color from coordinates
        if (evt.y >= pickerTop && evt.y < pickerTop + pickerHeight && evt.x >= pickerLeft && evt.x < pickerLeft + pickerWidth)
        {
            int px = evt.x - pickerLeft;
            int py = evt.y - pickerTop;

            float hue = float(px) / float(max(1, pickerWidth - 1)) * 360.0f;
            float sat = float(py) / float(max(1, pickerHeight - 1));
            // Limit sat to [0,1]
            if (sat < 0.0f) sat = 0.0f;
            if (sat > 1.0f) sat = 1.0f;

            uint16_t col = hsvTo565(hue, sat, 1.0f);
            selected = col;

            // Draw preview
            drawPreview(selected);
        }

        delay(20);
    }

    // If accepted, return selected (apply); otherwise original
    return accepted ? selected : initialColor;
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

        // Draw action buttons
        tft.fillRoundRect(20, tft.height() - 40, 100, 30, 5, Style::Colors::primary);
        tft.setTextColor(Style::Colors::text, Style::Colors::primary);
        tft.setTextDatum(MC_DATUM);
        tft.drawString("Save & Exit", 70, tft.height() - 25);

        tft.fillRoundRect(140, tft.height() - 40, 100, 30, 5, Style::Colors::danger);
        tft.setTextColor(Style::Colors::accentText, Style::Colors::danger);
        tft.drawString("Cancel", 190, tft.height() - 25);
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

            // Apply the theme in real-time when switching modes
            if (mode == "light")
            {
                working = defaultLight();
            }
            else if (mode == "dark")
            {
                working = defaultDark();
            }

            applyTheme(working);
            drawUI();
            continue;
        }

        // Custom colors touch -> open fullscreen picker
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
                    // Fullscreen color picker returns either original (cancel) or new color (ok)
                    uint16_t newColor = fullscreenColorPicker(working.colors[i]);
                    // If changed, update and apply
                    if (newColor != working.colors[i])
                    {
                        working.colors[i] = newColor;
                        applyTheme(working);
                    }
                    drawUI();
                    break;
                }
            }
        }

        // Save & Exit / Cancel buttons
        if (evt.y > tft.height() - 40 && evt.y < tft.height() - 10)
        {
            if (evt.x >= 20 && evt.x <= 120)
            {
                running = false;
                current = working;
                saveTheme(current);
                applyTheme(current);
                Serial.println("Designer exited, theme saved and applied.");
            }
            else if (evt.x >= 140 && evt.x <= 240)
            {
                running = false;
                applyTheme(current); // Revert to original theme
                Serial.println("Designer exited, changes discarded.");
            }
        }
    }
}
