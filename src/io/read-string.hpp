#pragma once

#include <Arduino.h>
#include <vector>
#include <algorithm>
#include <TFT_eSPI.h> // wichtig für MC_DATUM

#include "../screen/index.hpp"
using Screen::tft;

struct KeyRect
{
    int x, y, w, h;
    String label; // what to draw
    String value; // what to insert/command id
};

// ===== Layout constants =====
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const int MARGIN = 6;

static const int QUESTION_H = 20; // reserved space for question text
static const int KEY_W = 28;
static const int KEY_H = 32;
static const int KEY_SP = 4;

static const int AREA_X = MARGIN;
static const int AREA_Y = QUESTION_H + MARGIN; // text area starts below question
static const int AREA_W = SCREEN_W - 2 * MARGIN;
static const int AREA_H = 80; // smaller so keyboard always fits

// choose a single text size for the editable area (keine Mischung mehr)
static const int TEXT_SIZE_AREA = 1;

// ===== Utility =====
static inline int charWForSize(int textSize) { return 6 * textSize; }
static inline int lineHForSize(int textSize) { return (8 * textSize) + 4; }

static void drawKey(TFT_eSPI &tft, const KeyRect &k, bool pressed)
{
    uint16_t bg = pressed ? 0x8410 : 0xFFFF; // light gray when pressed
    uint16_t fg = pressed ? 0xFFFF : 0x0000;
    tft.fillRoundRect(k.x, k.y, k.w, k.h, 4, bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(fg);
    tft.setTextSize(1);
    tft.drawString(k.label, k.x + k.w / 2, k.y + k.h / 2);
}

static void drawKeyboard(TFT_eSPI &tft, const std::vector<KeyRect> &keys, int pressedIndex)
{
    for (size_t i = 0; i < keys.size(); ++i)
        drawKey(tft, keys[i], (int)i == pressedIndex);
}

static void drawTextArea(TFT_eSPI &tft,
                         const std::vector<String> &lines,
                         int scrollLine,
                         int cursorLine,
                         int cursorCol,
                         bool cursorVisible)
{
    const int pad = 6;
    // Hintergrund und Rahmen der Textarea
    tft.fillRect(AREA_X, AREA_Y, AREA_W, AREA_H, 0xFFFF);
    tft.drawRect(AREA_X, AREA_Y, AREA_W, AREA_H, 0x0000);

    tft.setTextSize(TEXT_SIZE_AREA);
    tft.setTextColor(0x0000);

    int lineH = lineHForSize(TEXT_SIZE_AREA);
    int visibleLines = AREA_H / lineH;
    for (int i = 0; i < visibleLines; ++i)
    {
        int li = scrollLine + i;
        if (li >= (int)lines.size())
            break;
        tft.setCursor(AREA_X + pad, AREA_Y + i * lineH + pad);
        tft.print(lines[li]);
    }

    // Draw cursor if visible and inside the visible area
    if (cursorVisible && cursorLine >= scrollLine && cursorLine < scrollLine + visibleLines)
    {
        int cy = AREA_Y + (cursorLine - scrollLine) * lineH + pad;
        int charW = charWForSize(TEXT_SIZE_AREA);
        int cx = AREA_X + pad + cursorCol * charW;
        // cursor height slightly smaller than line height
        int cursorH = lineH - 2;
        tft.drawFastVLine(cx, cy, cursorH, 0x0000);
    }
}

// ===== Keyboard builder =====
enum class KbMode
{
    LOWER,
    UPPER,
    NUMSYM
};

static std::vector<KeyRect> buildKeyboardLayout(KbMode mode)
{
    std::vector<KeyRect> keys;
    int y = AREA_Y + AREA_H + MARGIN; // keyboard starts below text area

    auto addRow = [&](const String &row, int yRow, int leftPad = 0, int keyW = KEY_W, int keySp = KEY_SP)
    {
        int totalW = row.length() * keyW + (row.length() - 1) * keySp;
        int startX = (SCREEN_W - totalW) / 2 + leftPad;
        int x = startX;
        for (size_t i = 0; i < row.length(); ++i)
        {
            String lab = String((char)row[i]);
            keys.push_back({x, yRow, keyW, KEY_H, lab, lab});
            x += keyW + keySp;
        }
    };

    if (mode == KbMode::LOWER || mode == KbMode::UPPER)
    {
        const char *r1 = (mode == KbMode::UPPER) ? "QWERTYUIOP" : "qwertyuiop";
        const char *r2 = (mode == KbMode::UPPER) ? "ASDFGHJKL" : "asdfghjkl";
        const char *r3 = (mode == KbMode::UPPER) ? "ZXCVBNM" : "zxcvbnm";

        // Row 1
        addRow(String(r1), y);
        y += KEY_H + KEY_SP;

        // Row 2
        addRow(String(r2), y);
        y += KEY_H + KEY_SP;

        // Row 3 with Shift + letters
        int x = MARGIN + 2;
        keys.push_back({x, y, 46, KEY_H, "Shift", "Shift"});
        x += 46 + KEY_SP;
        for (size_t i = 0; i < String(r3).length(); ++i)
        {
            String lab = String(r3[i]);
            keys.push_back({x, y, KEY_W, KEY_H, lab, lab});
            x += KEY_W + KEY_SP;
        }

        y += KEY_H + KEY_SP;
    }
    else
    { // NUMSYM
        addRow("1234567890", y);
        y += KEY_H + KEY_SP;
        addRow("!@#$%^&*()", y);
        y += KEY_H + KEY_SP;
        addRow("?[]{};:,.'\"", y, 0, 26, 4);
        y += KEY_H + KEY_SP;
    }

    y -= 8;
    // Bottom row = controls
    int sx = MARGIN;
    if (mode == KbMode::NUMSYM)
        keys.push_back({sx, y, 50, KEY_H, "ABC", "ABC"});
    else
        keys.push_back({sx, y, 50, KEY_H, "?123", "?123"});
    sx += 50 + KEY_SP;

    keys.push_back({sx, y, 70, KEY_H, "space", " "});
    sx += 70 + KEY_SP;

    keys.push_back({sx, y, 40, KEY_H, "<<", "BACK"});
    sx += 40 + KEY_SP;

    keys.push_back({sx, y, 40, KEY_H, "Del", "DEL"});
    sx += 40 + KEY_SP;

    keys.push_back({sx, y, 40, KEY_H, "\\n", "\n"});
    sx += 40 + KEY_SP;

    keys.push_back({sx, y, 40, KEY_H, "OK", "OK"});

    // buildKeyboardLayout zeichnet nicht den Screen — caller zeichnet nach Bedarf
    return keys;
}

// ===== Main text input =====
String readString(const String &question = "", const String &defaultValue = "")
{
    std::vector<String> lines;
    if (defaultValue.length() == 0)
        lines.push_back("");
    else
    {
        String tmp = defaultValue;
        int idx;
        while ((idx = tmp.indexOf('\n')) != -1)
        {
            lines.push_back(tmp.substring(0, idx));
            tmp = tmp.substring(idx + 1);
        }
        lines.push_back(tmp);
    }

    int scrollLine = 0;
    int cursorLine = lines.size() - 1;
    int cursorCol = lines[cursorLine].length();

    // initial screen clear + settings
    tft.fillScreen(0xFFFF);
    tft.setTextSize(TEXT_SIZE_AREA);
    tft.setTextColor(0x0000);

    KbMode mode = KbMode::LOWER;
    auto keys = buildKeyboardLayout(mode);

    bool cursorVisible = true;
    unsigned long lastBlink = millis();

    // initial draw
    drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, cursorVisible);
    drawKeyboard(tft, keys, -1);

    int pressedKey = -1;
    int lastHighlighted = -1;
    bool prevPressed = false;

    const int pad = 6;
    const int charW = charWForSize(TEXT_SIZE_AREA);

    while (true)
    {
        if (question.length())
        {
            tft.setCursor(MARGIN, MARGIN);
            tft.setTextSize(1); // question uses slightly larger font if desired
            tft.print(question);
            tft.setTextSize(TEXT_SIZE_AREA);
        }

        auto pos = Screen::getTouchPos();
        bool isPressed = pos.clicked;

        // Klick im Textbereich = Cursor setzen
        if (isPressed &&
            pos.x >= AREA_X && pos.x < AREA_X + AREA_W &&
            pos.y >= AREA_Y && pos.y < AREA_Y + AREA_H)
        {
            int relY = pos.y - AREA_Y;
            int lineH = lineHForSize(TEXT_SIZE_AREA);
            int clickedLine = scrollLine + relY / lineH;
            if (clickedLine < (int)lines.size())
            {
                cursorLine = clickedLine;
                int relX = pos.x - (AREA_X + pad);
                cursorCol = min(relX / charW, (int)lines[cursorLine].length());
                drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
            }
        }

        // Tastatur
        if (isPressed)
        {
            int found = -1;
            for (size_t i = 0; i < keys.size(); ++i)
            {
                KeyRect &k = keys[i];
                if (pos.x >= k.x && pos.x <= k.x + k.w &&
                    pos.y >= k.y && pos.y <= k.y + k.h)
                {
                    found = i;
                    break;
                }
            }
            if (found != lastHighlighted)
            {
                lastHighlighted = found;
                drawKeyboard(tft, keys, found);
            }
            pressedKey = found;
        }
        else if (!isPressed && prevPressed)
        {
            if (pressedKey != -1)
            {
                String val = keys[pressedKey].value;

                if (val == "Shift")
                {
                    mode = (mode == KbMode::LOWER) ? KbMode::UPPER : KbMode::LOWER;
                    keys = buildKeyboardLayout(mode);
                    tft.fillScreen(0xFFFF);
                    drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
                    drawKeyboard(tft, keys, -1);
                }
                else if (val == "?123" || val == "ABC")
                {
                    mode = (mode == KbMode::NUMSYM) ? KbMode::LOWER : KbMode::NUMSYM;
                    keys = buildKeyboardLayout(mode);
                    tft.fillScreen(0xFFFF);
                    drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
                    drawKeyboard(tft, keys, -1);
                }
                else if (val == "LEFT")
                {
                    if (cursorCol > 0)
                        cursorCol--;
                    else if (cursorLine > 0)
                    {
                        cursorLine--;
                        cursorCol = lines[cursorLine].length();
                    }
                }
                else if (val == "RIGHT")
                {
                    if (cursorCol < (int)lines[cursorLine].length())
                        cursorCol++;
                    else if (cursorLine < (int)lines.size() - 1)
                    {
                        cursorLine++;
                        cursorCol = 0;
                    }
                }
                else if (val == "UP")
                {
                    if (cursorLine > 0)
                    {
                        cursorLine--;
                        cursorCol = std::min(cursorCol, (int)lines[cursorLine].length());
                    }
                }
                else if (val == "DOWN")
                {
                    if (cursorLine < (int)lines.size() - 1)
                    {
                        cursorLine++;
                        cursorCol = std::min(cursorCol, (int)lines[cursorLine].length());
                    }
                }
                else if (val == "BACK")
                {
                    if (cursorCol > 0)
                    {
                        lines[cursorLine].remove(cursorCol - 1, 1);
                        cursorCol--;
                    }
                    else if (cursorLine > 0)
                    {
                        cursorCol = lines[cursorLine - 1].length();
                        lines[cursorLine - 1] += lines[cursorLine];
                        lines.erase(lines.begin() + cursorLine);
                        cursorLine--;
                    }
                }
                else if (val == "DEL")
                {
                    if (cursorCol < (int)lines[cursorLine].length())
                        lines[cursorLine].remove(cursorCol, 1);
                    else if (cursorLine < (int)lines.size() - 1)
                    {
                        lines[cursorLine] += lines[cursorLine + 1];
                        lines.erase(lines.begin() + cursorLine + 1);
                    }
                }
                else if (val == "OK")
                {
                    String result;
                    for (size_t i = 0; i < lines.size(); ++i)
                    {
                        result += lines[i];
                        if (i < lines.size() - 1)
                            result += "\n";
                    }
                    return result;
                }
                else if (val == "\n")
                {
                    // newline: split current line at cursorCol
                    String left = lines[cursorLine].substring(0, cursorCol);
                    String right = lines[cursorLine].substring(cursorCol);
                    lines[cursorLine] = left;
                    lines.insert(lines.begin() + cursorLine + 1, right);
                    cursorLine++;
                    cursorCol = 0;
                }
                else
                { // normal char insertion + auto-wrap if needed
                    lines[cursorLine] = lines[cursorLine].substring(0, cursorCol) + val + lines[cursorLine].substring(cursorCol);
                    cursorCol += val.length();

                    // auto-wrap: wenn Zeile breiter ist als die Area, splitten
                    int maxChars = (AREA_W - pad * 2) / charW;
                    if (maxChars < 1)
                        maxChars = 1;
                    if ((int)lines[cursorLine].length() > maxChars)
                    {
                        String overflow = lines[cursorLine].substring(maxChars);
                        lines[cursorLine] = lines[cursorLine].substring(0, maxChars);
                        lines.insert(lines.begin() + cursorLine + 1, overflow);
                        if (cursorCol > maxChars)
                        {
                            cursorLine++;
                            cursorCol -= maxChars;
                        }
                    }
                }

                // Scroll anpassen (line-wise)
                int visibleLines = AREA_H / lineHForSize(TEXT_SIZE_AREA);
                if (cursorLine < scrollLine)
                    scrollLine = cursorLine;
                else if (cursorLine >= scrollLine + visibleLines)
                    scrollLine = cursorLine - visibleLines + 1;

                // redraw
                tft.fillRect(AREA_X, AREA_Y, AREA_W, AREA_H, 0xFFFF); // clear text area only
                drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
            }
            pressedKey = -1;
            lastHighlighted = -1;
            drawKeyboard(tft, keys, -1); // Reset highlight
        }

        prevPressed = isPressed;

        // Blink cursor
        if (millis() - lastBlink > 500)
        {
            cursorVisible = !cursorVisible;
            lastBlink = millis();
            drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, cursorVisible);
        }

        delay(10);
    }
}
