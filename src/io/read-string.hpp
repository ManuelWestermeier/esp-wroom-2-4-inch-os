#pragma once

#include <Arduino.h>
#include <vector>
#include <algorithm>

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
static const int KEY_W = 26;
static const int KEY_H = 28;
static const int KEY_SP = 6;
static const int AREA_X = 6;
static const int AREA_Y = 6;
static const int AREA_W = 308; // 320 - margins
static const int AREA_H = 120;

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
    tft.fillRect(AREA_X, AREA_Y, AREA_W, AREA_H, 0xFFFF);
    tft.drawRect(AREA_X, AREA_Y, AREA_W, AREA_H, 0x0000);

    tft.setTextSize(1);
    tft.setTextColor(0x0000);

    int lineH = lineHForSize(1);
    int visibleLines = AREA_H / lineH;

    for (int i = 0; i < visibleLines; ++i)
    {
        int li = scrollLine + i;
        if (li >= (int)lines.size())
            break;
        tft.setCursor(AREA_X + pad, AREA_Y + i * lineH + pad);
        tft.print(lines[li]);
    }

    if (cursorVisible && cursorLine >= scrollLine && cursorLine < scrollLine + visibleLines)
    {
        int cy = AREA_Y + (cursorLine - scrollLine) * lineH + pad;
        int charW = charWForSize(1);
        int cx = AREA_X + pad + cursorCol * charW;
        tft.fillRect(cx, cy, 2, lineH - 2, 0x0000);
    }
}

// ===== Keyboard builder =====
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
    int y = 130 + MARGIN; // keyboard top

    auto addRow = [&](const String &row, int yRow, int leftPad = 0)
    {
        int totalW = row.length() * KEY_W + (row.length() - 1) * KEY_SP;
        int startX = (SCREEN_W - totalW) / 2 + leftPad;
        int x = startX;
        for (size_t i = 0; i < row.length(); ++i)
        {
            String lab = String((char)row[i]);
            keys.push_back({x, yRow, KEY_W, KEY_H, lab, lab});
            x += KEY_W + KEY_SP;
        }
    };

    if (mode == KbMode::LOWER || mode == KbMode::UPPER)
    {
        const char *r1 = (mode == KbMode::UPPER) ? "QWERTYUIOP" : "qwertyuiop";
        const char *r2 = (mode == KbMode::UPPER) ? "ASDFGHJKL" : "asdfghjkl";
        const char *r3 = (mode == KbMode::UPPER) ? "ZXCVBNM" : "zxcvbnm";

        addRow(String(r1), y);
        y += KEY_H + KEY_SP;
        addRow(String(r2), y);
        y += KEY_H + KEY_SP;

        // third row with Shift and arrows
        int totalW = String(r3).length() * KEY_W + (String(r3).length() - 1) * KEY_SP;
        int startX = (SCREEN_W - totalW) / 2;
        int x = startX;

        // Shift
        keys.push_back({MARGIN, y, 40, KEY_H, "Shift", "Shift"});
        for (size_t i = 0; i < String(r3).length(); ++i)
        {
            String lab = String(r3[i]);
            keys.push_back({x, y, KEY_W, KEY_H, lab, lab});
            x += KEY_W + KEY_SP;
        }
        // Arrow keys
        keys.push_back({SCREEN_W - MARGIN - 80, y, 18, KEY_H, "←", "LEFT"});
        keys.push_back({SCREEN_W - MARGIN - 60, y, 18, KEY_H, "→", "RIGHT"});
        keys.push_back({SCREEN_W - MARGIN - 40, y, 18, KEY_H, "↑", "UP"});
        keys.push_back({SCREEN_W - MARGIN - 20, y, 18, KEY_H, "↓", "DOWN"});
        y += KEY_H + KEY_SP;
    }
    else // NUMSYM
    {
        addRow("1234567890", y);
        y += KEY_H + KEY_SP;
        addRow("!@#$%^&*()-_=+", y); // special chars
        y += KEY_H + KEY_SP;
        addRow("[]{};:'\",.<>/?\\|`~", y); // more special chars
        y += KEY_H + KEY_SP;
    }

    // bottom row: mode switch, space, backspace, enter, delete, OK
    int sx = MARGIN;
    if (mode == KbMode::NUMSYM)
        keys.push_back({sx, y, 56, KEY_H, "ABC", "ABC"});
    else
        keys.push_back({sx, y, 56, KEY_H, "?123", "?123"});
    sx += 56 + KEY_SP;

    keys.push_back({sx, y, 120, KEY_H, "space", " "});
    sx += 120 + KEY_SP;

    keys.push_back({sx, y, 50, KEY_H, "⌫", "BACK"});
    sx += 50 + KEY_SP;

    keys.push_back({sx, y, 50, KEY_H, "Del", "DEL"});
    sx += 50 + KEY_SP;

    keys.push_back({sx, y, 50, KEY_H, "↵", "\n"});
    sx += 50 + KEY_SP;

    keys.push_back({sx, y, 40, KEY_H, "OK", "OK"});

    return keys;
}

// ===== Main text input =====
String readString(const String &question = "", const String &defaultValue = "")
{
    // Text buffer
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

    // UI
    tft.fillScreen(0xFFFF);
    tft.setTextSize(2);
    tft.setTextColor(0x0000);
    if (question.length())
    {
        tft.setCursor(6, 6);
        tft.print(question);
    }

    // Keyboard state
    KbMode mode = KbMode::LOWER;
    auto keys = buildKeyboardLayout(mode);

    // draw initial
    bool cursorVisible = true;
    unsigned long lastBlink = millis();
    drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, cursorVisible);
    drawKeyboard(tft, keys, -1);

    // Input loop (action on RELEASE to satisfy: "wait until you release")
    int pressedKey = -1;      // index while finger is down
    int lastHighlighted = -1; // to redraw only when changes
    bool prevPressed = false; // previous frame touch state

    while (true)
    {
        auto pos = Screen::getTouchPos();
        bool isPressed = pos.clicked; // true while touching

        if (isPressed)
        {
            // locate key under finger
            int found = -1;
            for (size_t i = 0; i < keys.size(); ++i)
            {
                const auto &k = keys[i];
                if (pos.x >= k.x && pos.x < k.x + k.w && pos.y >= k.y && pos.y < k.y + k.h)
                {
                    found = (int)i;
                    break;
                }
            }
            pressedKey = found;
            if (pressedKey != lastHighlighted)
            {
                drawKeyboard(tft, keys, pressedKey);
                lastHighlighted = pressedKey;
            }

            // Allow moving the cursor by dragging in the text area (no key under finger)
            if (pressedKey == -1 && pos.y >= AREA_Y && pos.y < AREA_Y + AREA_H)
            {
                int lineH = lineHForSize(1);
                int clickedLine = scrollLine + (pos.y - AREA_Y) / lineH;
                clickedLine = std::min(std::max(0, clickedLine), (int)lines.size() - 1);
                int col = (pos.x - (AREA_X + 6)) / charWForSize(1);
                col = std::min(std::max(0, col), (int)lines[clickedLine].length());
                cursorLine = clickedLine;
                cursorCol = col;
                drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
            }
        }

        // On RELEASE edge => perform action
        if (!isPressed && prevPressed)
        {
            // remove highlight
            if (lastHighlighted != -1)
            {
                drawKeyboard(tft, keys, -1);
                lastHighlighted = -1;
            }

            if (pressedKey != -1)
            {
                const KeyRect &k = keys[pressedKey];
                String val = k.value;
                if (val == "space")
                {
                    lines[cursorLine] += ' ';
                    cursorCol++;
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
                        int prevLen = lines[cursorLine - 1].length();
                        lines[cursorLine - 1] += lines[cursorLine];
                        lines.erase(lines.begin() + cursorLine);
                        cursorLine--;
                        cursorCol = prevLen;
                    }
                }
                else if (val == "DEL")
                {
                    if (cursorCol < (int)lines[cursorLine].length())
                    {
                        lines[cursorLine].remove(cursorCol, 1);
                    }
                    else if (cursorLine < (int)lines.size() - 1)
                    {
                        lines[cursorLine] += lines[cursorLine + 1];
                        lines.erase(lines.begin() + cursorLine + 1);
                    }
                }
                else if (val == "\n")
                {
                    String remainder = lines[cursorLine].substring(cursorCol);
                    lines[cursorLine] = lines[cursorLine].substring(0, cursorCol);
                    lines.insert(lines.begin() + cursorLine + 1, remainder);
                    cursorLine++;
                    cursorCol = 0;
                }
                else if (val == "OK")
                {
                    String out;
                    for (size_t i = 0; i < lines.size(); ++i)
                    {
                        out += lines[i];
                        if (i + 1 < lines.size())
                            out += '\n';
                    }
                    return out;
                }
                else if (val == "Shift")
                {
                    mode = (mode == KbMode::LOWER) ? KbMode::UPPER : KbMode::LOWER;
                    keys = buildKeyboardLayout(mode);
                    drawKeyboard(tft, keys, -1);
                }
                else if (val == "?123")
                {
                    mode = KbMode::NUMSYM;
                    keys = buildKeyboardLayout(mode);
                    drawKeyboard(tft, keys, -1);
                }
                else if (val == "ABC")
                {
                    mode = KbMode::LOWER;
                    keys = buildKeyboardLayout(mode);
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
                else
                {
                    // regular character (letters, numbers, symbols)
                    lines[cursorLine] += val; // value is 1-char label for normal keys
                    cursorCol += val.length();
                    if (mode == KbMode::UPPER)
                    { // auto-return to lower after single char? keep sticky Shift: DO NOT auto-return
                    }
                }

                // Maintain scroll so cursor stays visible
                int lineH = lineHForSize(1);
                int visibleLines = AREA_H / lineH;
                if (cursorLine < scrollLine)
                    scrollLine = cursorLine;
                if (cursorLine >= scrollLine + visibleLines)
                    scrollLine = cursorLine - visibleLines + 1;

                drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
            }
            pressedKey = -1;
        }

        // Blink cursor
        if (millis() - lastBlink > 500)
        {
            lastBlink = millis();
            cursorVisible = !cursorVisible;
            drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, cursorVisible);
        }

        prevPressed = isPressed;
        delay(16);
    }

    return String("");
}
