#pragma once

#include <Arduino.h>
#include <vector>
#include <algorithm>

#include "../screen/index.hpp"
using Screen::tft;

// NOTE: changed return type to String (by value) to avoid returning a reference to a local variable.
// Usage: String s = readString("Enter text:", "default");

struct KeyRect
{
    int x, y, w, h;
    String label;
};

static void drawKey(TFT_eSPI &tft, const KeyRect &k, bool pressed)
{
    uint16_t bg = pressed ? 0x8410 : 0xFFFF; // light gray when pressed
    uint16_t fg = pressed ? 0xFFFF : 0x0000;
    tft.fillRoundRect(k.x, k.y, k.w, k.h, 4, bg);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(fg);
    tft.setTextSize(1);
    tft.drawString(k.label, k.x + k.w / 2, k.y + k.h / 2);
}

static void drawKeyboard(TFT_eSPI &tft, const std::vector<KeyRect> &keys, int pressedIndex)
{
    for (size_t i = 0; i < keys.size(); ++i)
        drawKey(tft, keys[i], (int)i == pressedIndex);
}

static void drawTextArea(TFT_eSPI &tft, const std::vector<String> &lines, int scrollLine, int cursorLine, int cursorCol)
{
    const int pad = 6;
    const int areaX = 6;
    const int areaY = 6;
    const int areaW = 308; // 320 - margins
    const int areaH = 120;

    tft.fillRect(areaX, areaY, areaW, areaH, 0xFFFF);
    tft.drawRect(areaX, areaY, areaW, areaH, 0x0000);

    tft.setTextSize(1);
    tft.setTextColor(0x0000);

    int lineH = 10 + 2; // estimate; small font
    int visibleLines = areaH / lineH;

    for (int i = 0; i < visibleLines; ++i)
    {
        int li = scrollLine + i;
        if (li >= (int)lines.size())
            break;
        tft.setCursor(areaX + pad, areaY + i * lineH + pad);
        tft.print(lines[li]);
    }

    // draw cursor if visible (simple box)
    if (cursorLine >= scrollLine && cursorLine < scrollLine + visibleLines)
    {
        int cy = areaY + (cursorLine - scrollLine) * lineH + pad;
        // compute x of cursor approximately using char width
        int charW = 6; // approximate for textSize 1
        int cx = areaX + pad + cursorCol * charW;
        tft.fillRect(cx, cy, 1, lineH - 2, 0x0000);
    }
}

String readString(const String &question = "", const String &defaultValue = "")
{
    // Basic layout: top area = text area (6..126), bottom area = keyboard (130..240)
    const int kbY = 130;

    // Build a simple keyboard layout
    std::vector<String> rows = {
        "qwertyuiop",
        "asdfghjkl",
        "zxcvbnm",
    };

    // We'll add a bottom row with space, backspace, enter, shift, done
    std::vector<KeyRect> keys;
    const int margin = 6;
    const int keyW = 26;
    const int keyH = 28;
    const int spacing = 6;
    int y = kbY + margin;

    for (size_t r = 0; r < rows.size(); ++r)
    {
        String row = rows[r];
        int totalW = row.length() * keyW + (row.length() - 1) * spacing;
        int startX = (320 - totalW) / 2;
        int x = startX;
        for (size_t i = 0; i < row.length(); ++i)
        {
            KeyRect k{x, y, keyW, keyH, String((char)row[i])};
            keys.push_back(k);
            x += keyW + spacing;
        }
        y += keyH + spacing;
    }

    // bottom special row
    int bottomY = y;
    int sx = margin;
    KeyRect kSpace{sx, bottomY, 180, keyH, "space"};
    sx += kSpace.w + spacing;
    KeyRect kBack{sx, bottomY, 56, keyH, "<-"};
    sx += kBack.w + spacing;
    KeyRect kEnter{sx, bottomY, 56, keyH, "\n"};
    sx += kEnter.w + spacing;
    KeyRect kDone{sx, bottomY, 48, keyH, "OK"};

    keys.push_back(kSpace);
    keys.push_back(kBack);
    keys.push_back(kEnter);
    keys.push_back(kDone);

    // Setup text buffer
    std::vector<String> lines;
    if (defaultValue.length() == 0)
        lines.push_back("");
    else
    {
        // split default by newline
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

    // draw question if provided
    tft.fillScreen(0xFFFF);
    tft.setTextSize(2);
    tft.setTextColor(0x0000);
    if (question.length())
    {
        tft.setCursor(6, 6);
        tft.print(question);
    }

    // initial draw
    drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol);
    drawKeyboard(tft, keys, -1);

    // Input loop
    unsigned long lastBlink = millis();
    bool cursorVisible = true;
    int pressedKey = -1;
    int lastPressedKey = -1;
    unsigned long lastRepeat = 0;

    while (true)
    {
        auto pos = Screen::getTouchPos();

        // handle touch press to detect key press
        if (pos.clicked)
        {
            // find key under touch
            int found = -1;
            for (size_t i = 0; i < keys.size(); ++i)
            {
                KeyRect &k = keys[i];
                if (pos.x >= k.x && pos.x < k.x + k.w && pos.y >= k.y && pos.y < k.y + k.h)
                {
                    found = i;
                    break;
                }
            }
            if (found != -1)
            {
                pressedKey = found;
                if (pressedKey != lastPressedKey)
                {
                    // redraw keyboard with pressed highlight
                    drawKeyboard(tft, keys, pressedKey);
                    lastPressedKey = pressedKey;
                    lastRepeat = millis();
                }

                // handle key action on initial press
                KeyRect &k = keys[pressedKey];
                String label = k.label;
                if (label == "space")
                {
                    lines[cursorLine].concat(' ');
                    cursorCol++;
                }
                else if (label == "<-")
                {
                    // backspace
                    if (cursorCol > 0)
                    {
                        lines[cursorLine].remove(cursorCol - 1, 1);
                        cursorCol--;
                    }
                    else if (cursorLine > 0)
                    {
                        // merge lines
                        int prevLen = lines[cursorLine - 1].length();
                        lines[cursorLine - 1] += lines[cursorLine];
                        lines.erase(lines.begin() + cursorLine);
                        cursorLine--;
                        cursorCol = prevLen;
                    }
                }
                else if (label == "\n")
                {
                    // split the line at cursor
                    String remainder = lines[cursorLine].substring(cursorCol);
                    lines[cursorLine] = lines[cursorLine].substring(0, cursorCol);
                    lines.insert(lines.begin() + cursorLine + 1, remainder);
                    cursorLine++;
                    cursorCol = 0;
                }
                else if (label == "OK")
                {
                    // done
                    String out;
                    for (size_t i = 0; i < lines.size(); ++i)
                    {
                        out += lines[i];
                        if (i + 1 < lines.size())
                            out += '\n';
                    }
                    // clear keyboard area (optional)
                    return out;
                }
                else
                {
                    // regular letter
                    lines[cursorLine].concat(label);
                    cursorCol += label.length();
                }

                // redraw text area and keyboard
                drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol);
                drawKeyboard(tft, keys, pressedKey);
            }
            else
            {
                // touch outside keys: maybe scroll text area by dragging
                // we've been given pos.move.x/y; use vertical movement for scrolling
                if (pos.move.y != 0)
                {
                    int deltaLines = pos.move.y / 12; // some mapping
                    scrollLine = max(0, scrollLine - deltaLines);
                    drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol);
                }
                // also check if touch inside text area to set cursor approx
                const int areaX = 6;
                const int areaY = 6;
                const int areaW = 308;
                const int areaH = 120;
                if (pos.y >= areaY && pos.y < areaY + areaH)
                {
                    int lineH = 12;
                    int clickedLine = scrollLine + (pos.y - areaY) / lineH;
                    clickedLine = min(max(0, clickedLine), (int)lines.size() - 1);
                    // compute approx column by x
                    int col = (pos.x - (areaX + 6)) / 6;
                    col = min(max(0, col), (int)lines[clickedLine].length());
                    cursorLine = clickedLine;
                    cursorCol = col;
                    drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol);
                }
            }
        }
        else
        {
            // release: clear pressed highlight
            if (lastPressedKey != -1)
            {
                lastPressedKey = -1;
                drawKeyboard(tft, keys, -1);
            }
            pressedKey = -1;
        }

        // cursor blink
        if (millis() - lastBlink > 500)
        {
            lastBlink = millis();
            cursorVisible = !cursorVisible;
            // redraw small cursor only in text area
            drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol);
        }

        delay(20);
    }

    // unreachable, but keep compiler happy
    return String("");
}
