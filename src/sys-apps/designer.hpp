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
    vector<uint16_t> colors; // immer gespeichert, auch wenn light/dark
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
        ENC_FS::writeFileString(ENC_FS::str2Path("/settings/theme.txt"), content);
    SD_FS::writeFile("/settings/theme.txt", content);
}

static inline Theme loadTheme()
{
    bool fileExists = false;
    String content;

    if (Auth::username.isEmpty())
    {
        fileExists = SD_FS::exists("/settings/theme.txt");
        if (fileExists)
            content = SD_FS::readFile("/settings/theme.txt");
    }
    else
    {
        auto path = ENC_FS::str2Path("/settings/theme.txt");
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

    if (t.colors.size() < 10)
    {
        Theme def = (t.mode == "dark") ? defaultDark() : defaultLight();
        while (t.colors.size() < def.colors.size())
            t.colors.push_back(def.colors[t.colors.size()]);
    }

    return t;
}

// ---------- Apply ----------
static inline void applyTheme(const Theme &t)
{
    if (t.colors.size() < 10)
        return;

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

static inline void applyColorPalette()
{
    Theme t = loadTheme();
    applyTheme(t);
}

static inline uint16_t rgbTo565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// ---------- Fullscreen Color Picker (Hue x Value) ----------
static inline uint16_t fullscreenColorPicker(uint16_t initialColor)
{
    TFT_eSPI &tft = Screen::tft;

    tft.fillScreen(TFT_BLACK);

    const int btnW = 100, btnH = 40;
    const int okX = 20, okY = 12;
    const int cancelX = tft.width() - btnW - 20, cancelY = 12;
    const int pickerTop = okY + btnH + 8;
    const int pickerWidth = tft.width();
    const int pickerHeight = tft.height() - pickerTop - 20;

    uint16_t selected = initialColor;
    bool accepted = false;

    auto hsvTo565 = [&](float h, float s, float v) -> uint16_t
    {
        int i = int(h / 60.0f) % 6;
        float f = (h / 60.0f) - i;
        float p = v * (1 - s);
        float q = v * (1 - f * s);
        float t_val = v * (1 - (1 - f) * s);
        float r = 0, g = 0, b = 0;
        switch (i)
        {
        case 0:
            r = v;
            g = t_val;
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
            b = t_val;
            break;
        case 3:
            r = p;
            g = q;
            b = v;
            break;
        case 4:
            r = t_val;
            g = p;
            b = v;
            break;
        case 5:
            r = v;
            g = p;
            b = q;
            break;
        }
        return rgbTo565(r * 255, g * 255, b * 255);
    };

    // Gradient zeichnen
    for (int x = 0; x < pickerWidth; x++)
    {
        float hue = float(x) / float(pickerWidth - 1) * 360.0f;
        for (int y = 0; y < pickerHeight; y++)
        {
            float val = 1.0f - float(y) / float(pickerHeight - 1);
            uint16_t c = hsvTo565(hue, 1.0f, val);
            tft.drawPixel(x, pickerTop + y, c);
        }
    }

    // Buttons
    tft.fillRoundRect(okX, okY, btnW, btnH, 6, TFT_GREEN);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_BLACK, TFT_GREEN);
    tft.drawString("OK", okX + btnW / 2, okY + btnH / 2);

    tft.fillRoundRect(cancelX, cancelY, btnW, btnH, 6, TFT_RED);
    tft.setTextColor(TFT_WHITE, TFT_RED);
    tft.drawString("Cancel", cancelX + btnW / 2, cancelY + btnH / 2);

    auto drawPreview = [&](uint16_t c)
    {
        int cx = tft.width() / 2, cy = okY + btnH / 2;
        tft.fillCircle(cx, cy, 18, TFT_WHITE);
        tft.fillCircle(cx, cy, 16, c);
    };
    drawPreview(selected);

    while (true)
    {
        auto evt = Screen::getTouchPos();
        if (!evt.clicked)
        {
            delay(15);
            continue;
        }

        if (evt.x >= okX && evt.x <= okX + btnW && evt.y >= okY && evt.y <= okY + btnH)
        {
            accepted = true;
            break;
        }
        if (evt.x >= cancelX && evt.x <= cancelX + btnW && evt.y >= cancelY && evt.y <= cancelY + btnH)
        {
            break;
        }
        if (evt.y >= pickerTop && evt.y < pickerTop + pickerHeight)
        {
            float hue = float(evt.x) / float(pickerWidth - 1) * 360.0f;
            float val = 1.0f - float(evt.y - pickerTop) / float(pickerHeight - 1);
            selected = hsvTo565(hue, 1.0f, val);
            drawPreview(selected);
        }
    }
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

    int scrollOffset = 0; // Scroll
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
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(Style::Colors::text, Style::Colors::bg);
        tft.setTextSize(2);
        tft.drawString("Theme Designer", tft.width() / 2, 10);
        tft.setTextSize(1);

        // Mode buttons
        int x = 20;
        const char *modes[] = {"Light", "Dark", "Custom"};
        for (auto &m : modes)
        {
            bool sel = mode.equalsIgnoreCase(m);
            uint16_t bg = sel ? Style::Colors::accent : Style::Colors::primary;
            uint16_t fg = sel ? Style::Colors::accentText : Style::Colors::text;
            tft.fillRoundRect(x, 40, 80, buttonHeight, 5, bg);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(fg, bg);
            tft.drawString(m, x + 40, 40 + buttonHeight / 2);
            x += 90;
        }

        // Custom Palette
        if (mode.equalsIgnoreCase("custom"))
        {
            int y = topMargin;
            int xStart = 20 + scrollOffset;
            for (size_t i = 0; i < working.colors.size(); i++)
            {
                int cx = xStart + i * (colorBoxSize + spacing);
                int cy = y + 20;
                if (cx + colorBoxSize < 0 || cx > tft.width())
                    continue;

                tft.setTextDatum(TC_DATUM);
                tft.setTextColor(Style::Colors::text, Style::Colors::bg);
                tft.drawString(colorNames[i], cx + colorBoxSize / 2, y);

                tft.fillRoundRect(cx, cy, colorBoxSize, colorBoxSize, 5, working.colors[i]);
                tft.drawRoundRect(cx, cy, colorBoxSize, colorBoxSize, 5, Style::Colors::text);
            }
        }

        // Action Buttons
        tft.fillRoundRect(20, tft.height() - 40, 100, 30, 5, Style::Colors::primary);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(Style::Colors::text, Style::Colors::primary);
        tft.drawString("Save & Exit", 70, tft.height() - 25);

        tft.fillRoundRect(140, tft.height() - 40, 100, 30, 5, Style::Colors::danger);
        tft.setTextColor(Style::Colors::accentText, Style::Colors::danger);
        tft.drawString("Cancel", 190, tft.height() - 25);
    };

    drawUI();

    while (running)
    {
        auto evt = Screen::getTouchPos();
        if (!evt.clicked && evt.move.x == 0)
        {
            delay(15);
            continue;
        }

        // Scroll
        if (evt.move.x != 0)
        {
            scrollOffset += evt.move.x;
            if (scrollOffset > 0)
                scrollOffset = 0;

            int paletteWidth = working.colors.size() * (colorBoxSize + spacing);
            if (paletteWidth + scrollOffset < tft.width())
                scrollOffset = tft.width() - paletteWidth;

            drawUI();
            continue;
        }

        // Mode switching
        if (evt.y >= 40 && evt.y <= 70)
        {
            if (evt.x >= 20 && evt.x <= 100)
                mode = "light";
            else if (evt.x >= 110 && evt.x <= 190)
                mode = "dark";
            else if (evt.x >= 200 && evt.x <= 280)
                mode = "custom";
            working.mode = mode;
            if (mode == "light")
                working = defaultLight();
            else if (mode == "dark")
                working = defaultDark();
            applyTheme(working);
            drawUI();
            continue;
        }

        // Custom color edit
        if (mode == "custom")
        {
            int xStart = 20 + scrollOffset;
            int y = topMargin;
            for (size_t i = 0; i < working.colors.size(); i++)
            {
                int cx = xStart + i * (colorBoxSize + spacing);
                int cy = y + 20;
                if (evt.x >= cx && evt.x <= cx + colorBoxSize &&
                    evt.y >= cy && evt.y <= cy + colorBoxSize)
                {
                    uint16_t nc = fullscreenColorPicker(working.colors[i]);
                    working.colors[i] = nc;
                    applyTheme(working);
                    drawUI();
                }
            }
        }

        // Save/Cancel
        if (evt.y > tft.height() - 40 && evt.y < tft.height() - 10)
        {
            if (evt.x >= 20 && evt.x <= 120)
            {
                current = working;
                saveTheme(current);
                applyTheme(current);
                running = false;
            }
            else if (evt.x >= 140 && evt.x <= 240)
            {
                applyTheme(current);
                running = false;
            }
        }
    }
}
