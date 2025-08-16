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

struct NavItem
{
    int x, w;     // x in the virtual strip (not screen), width
    String label; // what to draw
    String value; // what to do/insert
};

// ===== Layout constants =====
static const int SCREEN_W = 320;
static const int SCREEN_H = 240;
static const int MARGIN = 6;

static const int QUESTION_H = 20; // reserved space for question text
static const int KEY_W = 27;      // chosen so 10 keys + spacing fits 308px
static const int KEY_H = 28;
static const int KEY_SP = 4;

static const int NAV_H = 28;  // black navigation bar height
static const int NAV_SP = 4;  // spacing between nav items
static const int NAV_PAD = 6; // inner padding per nav item

static const int AREA_X = MARGIN;
static const int AREA_W = SCREEN_W - 2 * MARGIN;

// Derived vertical positions (computed at runtime for robustness)
static inline int navY() { return SCREEN_H - NAV_H - MARGIN; }
static inline int keyRowsTotalH() { return KEY_H * 3 + KEY_SP * 2; } // exactly 3 rows
static inline int keysY() { return navY() - keyRowsTotalH() - MARGIN; }
static inline int textY() { return (QUESTION_H > 0) ? (MARGIN + QUESTION_H + MARGIN) : MARGIN; }
static inline int textH()
{
    int h = keysY() - textY() - MARGIN;
    return (h < 24) ? 24 : h; // ensure at least one visible line
}

// ===== Utility =====
static inline int clampi(int v, int lo, int hi) { return (v < lo) ? lo : (v > hi ? hi : v); }
static inline int charWForSize(int textSize) { return 6 * textSize; }
static inline int lineHForSize(int textSize) { return (8 * textSize) + 4; }

// ===== Drawing: keys =====
static void drawKey(TFT_eSPI &tft, const KeyRect &k, bool pressed)
{
    uint16_t bg = pressed ? 0x8410 : 0xFFFF; // light gray pressed
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

// ===== Drawing: text area =====
static void drawTextArea(TFT_eSPI &tft,
                         const std::vector<String> &lines,
                         int scrollLine,
                         int cursorLine,
                         int cursorCol,
                         bool cursorVisible)
{
    const int TX = AREA_X;
    const int TY = textY();
    const int TW = AREA_W;
    const int TH = textH();
    const int pad = 6;

    tft.fillRect(TX, TY, TW, TH, 0xFFFF);
    tft.drawRect(TX, TY, TW, TH, 0x0000);

    tft.setTextSize(1);
    tft.setTextColor(0x0000);

    int lineH = lineHForSize(1);
    int visibleLines = TH / lineH;

    for (int i = 0; i < visibleLines; ++i)
    {
        int li = scrollLine + i;
        if (li >= (int)lines.size())
            break;
        tft.setCursor(TX + pad, TY + i * lineH + pad);
        tft.print(lines[li]);
    }

    if (cursorVisible && cursorLine >= scrollLine && cursorLine < scrollLine + visibleLines)
    {
        int cy = TY + (cursorLine - scrollLine) * lineH + pad;
        int charW = charWForSize(1);
        int cx = TX + pad + cursorCol * charW;
        tft.drawFastVLine(cx, cy, lineH - 2, 0x0000);
    }
}

// ===== Keyboard builder =====
enum class KbMode
{
    LOWER,
    UPPER,
    NUMSYM
};

void buildKeyboardLayout(std::vector<Key> &keys, KbMode mode)
{
    keys.clear();

    // Letter rows (QWERTY)
    std::vector<std::string> rows;
    if (mode == KbMode::ABC)
    {
        rows = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    }
    else
    { // NUMSYM
        rows = {"1234567890", "!@#$%^&*()", "-_=+[]{};", ":'\",.<>/?\\|"};
    }

    int y = AREA_Y + AREA_H + MARGIN;
    for (size_t r = 0; r < rows.size(); r++)
    {
        std::string row = rows[r];
        int totalW = row.size() * (KEY_W + KEY_SP) - KEY_SP;
        int x = (SCREEN_W - totalW) / 2; // center row

        for (char c : row)
        {
            std::string label(1, c);
            keys.push_back({x, y, KEY_W, KEY_H, label, label});
            x += KEY_W + KEY_SP;
        }
        y += KEY_H + KEY_SP;
    }

    // --- Navigation bar at the bottom ---
    int navY = SCREEN_H - NAVBAR_H;
    int navX = MARGIN;

    // Scrollable navigation strip
    // (all on one line, horizontally)
    std::vector<std::pair<std::string, std::string>> navKeys = {
        {"Shift", "SHIFT"},
        {mode == KbMode::ABC ? "?123" : "ABC", mode == KbMode::ABC ? "?123" : "ABC"},
        {"Space", " "},
        {"Bksp", "BACK"},
        {"Del", "DEL"},
        {"⏎", "\n"},
        {"OK", "OK"}};

    for (auto &nk : navKeys)
    {
        keys.push_back({navX, navY, NAVKEY_W, NAVBAR_H - 2, nk.first, nk.second});
        navX += NAVKEY_W + KEY_SP;
    }
}

// ===== Nav bar builder/draw =====
static int navItemWidth(const String &label)
{
    // robust width: text + paddings, minimum 28
    int w = (int)label.length() * charWForSize(1) + 2 * NAV_PAD;
    if (w < 28)
        w = 28;
    return w;
}

static std::vector<NavItem> buildNavItems(KbMode mode)
{
    // Order defines the strip (will be draggable to reveal all)
    std::vector<String> labels;
    std::vector<String> values;

    // Mode toggle + shift
    labels.push_back(mode == KbMode::NUMSYM ? "ABC" : "?123");
    values.push_back("MODE");
    labels.push_back("aA");
    values.push_back("SHIFT");

    // Navigation & editing
    labels.push_back("⏎");
    values.push_back("\n");
    labels.push_back("Back");
    values.push_back("BACK");
    labels.push_back("Del");
    values.push_back("DEL");
    labels.push_back("Home");
    values.push_back("HOME");
    labels.push_back("End");
    values.push_back("END");
    labels.push_back("←");
    values.push_back("LEFT");
    labels.push_back("→");
    values.push_back("RIGHT");
    labels.push_back("↑");
    values.push_back("UP");
    labels.push_back("↓");
    values.push_back("DOWN");

    // Common punctuation (kept here to avoid a 4th keyboard row)
    const char *punct[] = {".", ",", "-", "_", "/", "\\", ":", ";", "'", "\"", "<", ">", "?", "!"};
    for (auto p : punct)
    {
        labels.push_back(p);
        values.push_back(p);
    }

    // Finalize
    labels.push_back("OK");
    values.push_back("OK");

    std::vector<NavItem> items;
    int x = 0;
    for (size_t i = 0; i < labels.size(); ++i)
    {
        int w = navItemWidth(labels[i]);
        items.push_back({x, w, labels[i], values[i]});
        x += w + NAV_SP;
    }
    return items;
}

static void drawNavBar(TFT_eSPI &tft,
                       const std::vector<NavItem> &items,
                       int scrollX,
                       int pressedIndex)
{
    const int NY = navY();
    const int NX = AREA_X;
    const int NW = AREA_W;

    // Background black bar
    tft.fillRect(NX, NY, NW, NAV_H, 0x0000);

    // Draw visible items
    tft.setTextDatum(MC_DATUM);
    tft.setTextSize(1);

    for (size_t i = 0; i < items.size(); ++i)
    {
        int sx = NX + (items[i].x - scrollX); // on-screen x after scrolling
        int sw = items[i].w;
        int sy = NY + 2;
        int sh = NAV_H - 4;

        if (sx + sw < NX || sx > NX + NW)
            continue; // not visible

        bool pressed = ((int)i == pressedIndex);
        uint16_t bg = pressed ? 0xFFFF : 0x4208; // pressed: white, else dark gray
        uint16_t fg = pressed ? 0x0000 : 0xFFFF; // text invert if pressed

        tft.fillRoundRect(sx, sy, sw, sh, 4, bg);
        tft.setTextColor(fg);
        tft.drawString(items[i].label, sx + sw / 2, sy + sh / 2);
    }

    // Optional visual hint for scroll (simple fade bars)
    // left hint
    // right hint
    // (kept minimal to save cycles)
}

// ===== Main text input =====
String readString(const String &question = "", const String &defaultValue = "")
{
    // Prepare lines
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

    // Screen setup
    tft.fillScreen(0xFFFF);
    tft.setTextSize(2);
    tft.setTextColor(0x0000);

    if (question.length())
    {
        tft.setCursor(MARGIN, MARGIN);
        tft.print(question);
    }

    // Keyboard/nav state
    KbMode mode = KbMode::LOWER;
    auto keys = buildKeyboardLayout(mode);
    auto nav = buildNavItems(mode);

    bool cursorVisible = true;
    unsigned long lastBlink = millis();

    // Nav scroll/drag state
    int navScrollX = 0;
    int navTotalW = (nav.empty() ? 0 : (nav.back().x + nav.back().w));
    int navMaxScroll = std::max(0, navTotalW - AREA_W);

    int pressedKey = -1;
    int lastKeyHighlighted = -1;

    int pressedNav = -1;
    bool navDragging = false;
    int navDragStartX = 0;
    int navDragLastX = 0;
    bool navMoved = false;

    bool prevPressed = false;

    // First draw
    drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, cursorVisible);
    drawKeyboard(tft, keys, -1);
    drawNavBar(tft, nav, navScrollX, -1);

    auto ensureScrollIntoView = [&]()
    {
        int visibleLines = textH() / lineHForSize(1);
        if (cursorLine < scrollLine)
            scrollLine = cursorLine;
        else if (cursorLine >= scrollLine + visibleLines)
            scrollLine = cursorLine - visibleLines + 1;
        if (scrollLine < 0)
            scrollLine = 0;
    };

    auto insertStringAtCursor = [&](const String &s)
    {
        lines[cursorLine] = lines[cursorLine].substring(0, cursorCol) + s + lines[cursorLine].substring(cursorCol);
        cursorCol += s.length();
    };

    while (true)
    {
        auto pos = Screen::getTouchPos();
        bool isPressed = pos.clicked;

        const int NY = navY();
        const int KY = keysY();
        const int KH = keyRowsTotalH();
        const int TY = textY();
        const int TH = textH();

        // ===== Text area click: set cursor
        if (isPressed &&
            pos.x >= AREA_X && pos.x < AREA_X + AREA_W &&
            pos.y >= TY && pos.y < TY + TH)
        {
            int relY = pos.y - TY;
            int lineH = lineHForSize(1);
            int clickedLine = scrollLine + relY / lineH;
            if (clickedLine < (int)lines.size())
            {
                cursorLine = clickedLine;
                int relX = pos.x - (AREA_X + 6);
                int charW = charWForSize(1);
                cursorCol = std::min(relX / charW, (int)lines[cursorLine].length());
                drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
            }
        }

        // ===== Keyboard area
        if (isPressed &&
            pos.x >= AREA_X && pos.x < AREA_X + AREA_W &&
            pos.y >= KY && pos.y < KY + KH)
        {
            int found = -1;
            for (size_t i = 0; i < keys.size(); ++i)
            {
                const KeyRect &k = keys[i];
                if (pos.x >= k.x && pos.x <= k.x + k.w &&
                    pos.y >= k.y && pos.y <= k.y + k.h)
                {
                    found = (int)i;
                    break;
                }
            }
            if (found != lastKeyHighlighted)
            {
                lastKeyHighlighted = found;
                drawKeyboard(tft, keys, found);
            }
            pressedKey = found;
        }
        else if (!isPressed && prevPressed && pressedKey != -1)
        {
            // Commit key
            String val = keys[pressedKey].value;
            insertStringAtCursor(val);

            ensureScrollIntoView();
            drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
            pressedKey = -1;
            lastKeyHighlighted = -1;
            drawKeyboard(tft, keys, -1);
        }

        // ===== Nav bar area: press/drag/release
        if (pos.y >= NY && pos.y < NY + NAV_H)
        {
            if (isPressed)
            {
                if (!prevPressed) // new touch
                {
                    navDragging = false;
                    navMoved = false;
                    navDragStartX = navDragLastX = pos.x;
                    // detect hit on any item (without scroll yet considered below)
                    int found = -1;
                    for (size_t i = 0; i < nav.size(); ++i)
                    {
                        int sx = AREA_X + (nav[i].x - navScrollX);
                        int sw = nav[i].w;
                        if (pos.x >= sx && pos.x <= sx + sw)
                        {
                            found = (int)i;
                            break;
                        }
                    }
                    pressedNav = found;
                    drawNavBar(tft, nav, navScrollX, pressedNav);
                }
                else
                {
                    // movement -> drag scroll
                    int dx = pos.x - navDragLastX;
                    if (abs(pos.x - navDragStartX) > 6)
                    {
                        navMoved = true;
                        navDragging = true;
                    }
                    if (navDragging && navMaxScroll > 0)
                    {
                        navScrollX = clampi(navScrollX - dx, 0, navMaxScroll);
                        navDragLastX = pos.x;
                        // while dragging, do not highlight any item
                        drawNavBar(tft, nav, navScrollX, -1);
                    }
                }
            }
            else if (!isPressed && prevPressed)
            {
                // release
                if (!navDragging && pressedNav != -1)
                {
                    String val = nav[pressedNav].value;

                    if (val == "MODE")
                    {
                        mode = (mode == KbMode::NUMSYM) ? KbMode::LOWER : KbMode::NUMSYM;
                        keys = buildKeyboardLayout(mode);
                        nav = buildNavItems(mode);
                        navTotalW = (nav.empty() ? 0 : (nav.back().x + nav.back().w));
                        navMaxScroll = std::max(0, navTotalW - AREA_W);
                        navScrollX = clampi(navScrollX, 0, navMaxScroll);

                        drawKeyboard(tft, keys, -1);
                        drawNavBar(tft, nav, navScrollX, -1);
                    }
                    else if (val == "SHIFT")
                    {
                        mode = (mode == KbMode::LOWER)   ? KbMode::UPPER
                               : (mode == KbMode::UPPER) ? KbMode::LOWER
                                                         : KbMode::NUMSYM; // keep NUMSYM if currently NUMSYM
                        keys = buildKeyboardLayout(mode);
                        drawKeyboard(tft, keys, -1);
                        drawNavBar(tft, nav, navScrollX, -1);
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
                        drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
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
                        drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
                    }
                    else if (val == "UP")
                    {
                        if (cursorLine > 0)
                        {
                            cursorLine--;
                            cursorCol = std::min(cursorCol, (int)lines[cursorLine].length());
                        }
                        drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
                    }
                    else if (val == "DOWN")
                    {
                        if (cursorLine < (int)lines.size() - 1)
                        {
                            cursorLine++;
                            cursorCol = std::min(cursorCol, (int)lines[cursorLine].length());
                        }
                        drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
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
                        ensureScrollIntoView();
                        drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
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
                        drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
                    }
                    else if (val == "HOME")
                    {
                        cursorCol = 0;
                        drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
                    }
                    else if (val == "END")
                    {
                        cursorCol = lines[cursorLine].length();
                        drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
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
                        // newline: split current line
                        String tail = lines[cursorLine].substring(cursorCol);
                        lines[cursorLine] = lines[cursorLine].substring(0, cursorCol);
                        lines.insert(lines.begin() + cursorLine + 1, tail);
                        cursorLine++;
                        cursorCol = 0;
                        ensureScrollIntoView();
                        drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
                    }
                    else
                    {
                        // insert punctuation or literal value
                        insertStringAtCursor(val);
                        ensureScrollIntoView();
                        drawTextArea(tft, lines, scrollLine, cursorLine, cursorCol, true);
                    }
                }

                pressedNav = -1;
                navDragging = false;
                navMoved = false;
                drawNavBar(tft, nav, navScrollX, -1);
            }
        }
        else
        {
            // leaving nav area while pressed -> stop highlight
            if (!isPressed && prevPressed && pressedNav != -1)
            {
                pressedNav = -1;
                drawNavBar(tft, nav, navScrollX, -1);
            }
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
