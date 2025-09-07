#pragma once
// file: file-picker-clean.hpp
// Cleaner, rounder, non-overlapping UI file/folder picker for 320x240 TFT using ENC_FS and Screen
// Returns the selected plain path as "/foo/bar.txt" or empty String if cancelled.
// All returned paths start with '/' and folder paths (except root "/") do NOT end with '/'.

#include <Arduino.h>
#include <vector>
#include <algorithm>
#include <cmath>

#include "../fs/enc-fs.hpp"    // provides ENC_FS::*
#include "../screen/index.hpp" // provides Screen::tft and Screen::getTouchPos()
#include "../utils/vec.hpp"
#include "../utils/rect.hpp"
#include "../styles/global.hpp"

using std::vector;

namespace FilePicker
{
    using Path = ENC_FS::Path;

    // ---- Layout constants (tweak for visual balance) ----
    static constexpr int FP_SCREEN_W = 320;
    static constexpr int FP_SCREEN_H = 240;

    // Safe paddings so nothing ever overlaps.
    static constexpr int OUTER_PAD = 8;

    // Header/footer dimensions and exact placement so the list area is strictly between them.
    static constexpr int FP_HEADER_H = 48;                                      // slightly taller header for round look
    static constexpr int FP_FOOTER_H = 48;                                      // footer bar height
    static constexpr int FP_HEADER_TOP = OUTER_PAD;                             // Y of header
    static constexpr int FP_FOOTER_TOP = FP_SCREEN_H - OUTER_PAD - FP_FOOTER_H; // Y of footer

    // List area fits exactly between header bottom and footer top minus inner padding
    static constexpr int FP_INNER_V_PAD = 8;
    static constexpr int FP_LIST_Y = FP_HEADER_TOP + FP_HEADER_H + FP_INNER_V_PAD;
    static constexpr int FP_LIST_BOTTOM = FP_FOOTER_TOP - FP_INNER_V_PAD;
    static constexpr int FP_LIST_H = FP_LIST_BOTTOM - FP_LIST_Y;

    // left/right padding and item size
    static constexpr int FP_LIST_X = 12;
    static constexpr int FP_LIST_W = FP_SCREEN_W - FP_LIST_X * 2;
    static constexpr int FP_ITEM_H = 44;                                 // comfortable item height for rounded UI
    static constexpr int FP_VISIBLE_ITEMS = (FP_LIST_H / FP_ITEM_H) + 1; // allow partial at bottom

    // visual constants (rounder)
    static constexpr int ICON_X_OFFSET = 14;
    static constexpr int ICON_WIDTH = 26;
    static constexpr int CHEVRON_W = 6;
    static constexpr int TAP_THRESHOLD = 8; // px move allowed to count as tap
    static constexpr int MAX_OVERSCROLL = 36;
    static constexpr float SCROLL_FRICTION = 0.92f;
    static constexpr unsigned long TAP_MAX_TIME = 420; // ms

    static constexpr int CORNER_RADIUS = 12; // roundness for header/footer/buttons
    static constexpr int ITEM_RADIUS = 8;    // slight rounding for pressed item overlay
    static constexpr int BUTTON_RADIUS = 12; // button corner rounding

    // Normalize an incoming path string so:
    //  - it always starts with '/'
    //  - it never ends with '/' unless it is the root "/"
    static String normalizePathString(const String &in)
    {
        if (in.length() == 0)
            return String("/");

        String s = in;
        if (!s.startsWith("/"))
            s = String("/") + s;

        while (s.length() > 1 && s.endsWith("/"))
            s.remove(s.length() - 1);

        return s;
    }

    static String pathToString(const Path &p)
    {
        if (p.empty())
            return String("/");
        String out;
        for (size_t i = 0; i < p.size(); ++i)
        {
            out += "/";
            out += p[i];
        }
        return out;
    }

    static Path stringToPath(const String &s)
    {
        String norm = normalizePathString(s);
        return ENC_FS::str2Path(norm);
    }

    // simple chevron/back icon (flat)
    static void drawBackChevron(int x, int y, int size = 12)
    {
        Screen::tft.fillTriangle(x + size, y, x, y + size / 2, x + size, y + size, TEXT);
    }

    // draw header: rounded bar, breadcrumb text, back icon left
    static void drawHeader(const String &currentPathStr)
    {
        auto &tft = Screen::tft;
        // clear exactly the header area (prevents overlap)
        tft.fillRect(0, FP_HEADER_TOP - 2, FP_SCREEN_W, FP_HEADER_H + 4, BG);

        // header background (rounded)
        tft.fillRoundRect(OUTER_PAD, FP_HEADER_TOP, FP_SCREEN_W - OUTER_PAD * 2, FP_HEADER_H, CORNER_RADIUS, PRIMARY);

        // Back chevron (touch area inside outer pad)
        int chevronX = OUTER_PAD + 12;
        int chevronY = FP_HEADER_TOP + (FP_HEADER_H - 12) / 2;
        drawBackChevron(chevronX, chevronY);

        // Breadcrumb (shortened if too long)
        String display = currentPathStr;
        if (display.length() > 30)
        {
            int len = display.length();
            int start = display.lastIndexOf('/', len - 2); // second last slash
            if (start > 0)
                display = String("...") + display.substring(start);
        }

        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(1);
        tft.setTextColor(TEXT, PRIMARY);
        tft.drawString(display, chevronX + 22, FP_HEADER_TOP + (FP_HEADER_H / 2) - 9, 2);
    }

    // draw footer: rounded bar with Cancel / Select and page indicator (no overlap)
    static void drawFooter(int pageIndex, int totalPages)
    {
        auto &tft = Screen::tft;
        // clear exactly the footer area
        tft.fillRect(0, FP_FOOTER_TOP - 2, FP_SCREEN_W, FP_FOOTER_H + 4, BG);

        // footer background (rounded)
        tft.fillRoundRect(OUTER_PAD, FP_FOOTER_TOP, FP_SCREEN_W - OUTER_PAD * 2, FP_FOOTER_H, CORNER_RADIUS, PRIMARY);

        // Buttons
        int btnW = 92;
        int btnH = FP_FOOTER_H - 16;
        int bx = OUTER_PAD + 12;
        int by = FP_FOOTER_TOP + 8;
        int sx = FP_SCREEN_W - OUTER_PAD - 12 - btnW;

        // Cancel (filled with DANGER - red)
        tft.fillRoundRect(bx, by, btnW, btnH, BUTTON_RADIUS, DANGER);
        tft.setTextDatum(TC_DATUM);
        tft.setTextSize(1);
        // draw centered text (foreground = TEXT, background = DANGER so anti-aliased/clean)
        tft.setTextColor(TEXT, DANGER);
        tft.drawString("Cancel", bx + btnW / 2, by + btnH / 2 - 8, 2);

        // Select (accent filled)
        tft.fillRoundRect(sx, by, btnW, btnH, BUTTON_RADIUS, ACCENT);
        tft.setTextColor(TEXT, ACCENT);
        tft.drawString("Select", sx + btnW / 2, by + btnH / 2 - 8, 2);

        // Page indicator centered (use PRIMARY as BG so it's readable)
        String pstr = String(pageIndex + 1) + "/" + String(std::max(1, totalPages));
        tft.setTextDatum(TC_DATUM);
        tft.setTextColor(TEXT, PRIMARY);
        tft.drawString(pstr, FP_SCREEN_W / 2, by + btnH / 2 - 8, 2);
    }

    // Read directory entries (plain names) and metadata flag vector (isDir)
    static void readEntries(const Path &dirPath, vector<String> &names, vector<bool> &isDir)
    {
        names.clear();
        isDir.clear();
        vector<String> n = ENC_FS::readDir(dirPath);

        // sort lexicographically case-insensitive
        std::sort(n.begin(), n.end(), [](const String &a, const String &b)
                  {
                      String aa = a; aa.toLowerCase();
                      String bb = b; bb.toLowerCase();
                      return aa < bb; });

        for (auto &entry : n)
        {
            Path child = dirPath;
            child.push_back(entry);
            ENC_FS::Metadata m = ENC_FS::getMetadata(child);
            names.push_back(entry);
            isDir.push_back(m.isDirectory);
        }

        // stable partition: directories first, keep order
        vector<String> nn;
        vector<bool> nd;
        for (size_t i = 0; i < names.size(); ++i)
            if (isDir[i])
            {
                nn.push_back(names[i]);
                nd.push_back(true);
            }
        for (size_t i = 0; i < names.size(); ++i)
            if (!isDir[i])
            {
                nn.push_back(names[i]);
                nd.push_back(false);
            }
        names.swap(nn);
        isDir.swap(nd);
    }

    // Main file picker
    static String filePickerImpl(String startPath = "/")
    {
        auto &tft = Screen::tft;

        // normalize incoming start path and convert to Path
        String normStart = normalizePathString(startPath);
        Path curPath = ENC_FS::str2Path(normStart);
        String curPathStr = ENC_FS::path2Str(curPath); // normalized

        // read directory
        vector<String> entries;
        vector<bool> entriesIsDir;
        readEntries(curPath, entries, entriesIsDir);

        // scrolling state
        float scrollY = 0.0f; // logical scroll offset in px (0 = top)
        float velocity = 0.0f;
        int totalItems = (int)entries.size();
        int contentHeight = totalItems * FP_ITEM_H;
        int viewportHeight = FP_LIST_H;
        int maxScroll = std::max(0, contentHeight - viewportHeight);

        int page = 0;
        int totalPages = std::max(1, (int)((entries.size() + FP_VISIBLE_ITEMS - 1) / FP_VISIBLE_ITEMS));

        // initial background
        tft.fillScreen(BG);

        // draw static chrome
        drawHeader(curPathStr);
        drawFooter(page, totalPages);

        // We'll render the list inside a viewport using setViewport.
        // We'll render items with y positions relative to scrollY.
        // Use a helper to redraw list region.
        auto redrawList = [&]()
        {
            // clear the list viewport background area (exact bounds, no overlap)
            tft.fillRect(FP_LIST_X, FP_LIST_Y, FP_LIST_W, FP_LIST_H, BG);

            // set viewport so drawing is clipped to list area (TFT_eSPI provides setViewport)
            tft.setViewport(FP_LIST_X, FP_LIST_Y, FP_LIST_W, FP_LIST_H, true);

            // clamp scroll for draw decisions
            float drawScroll = scrollY;
            if (drawScroll < 0.0f)
                drawScroll = 0.0f;
            if (drawScroll > maxScroll)
                drawScroll = (float)maxScroll;

            int firstVisible = std::max(0, (int)floor(drawScroll / FP_ITEM_H));
            int lastVisible = std::min(totalItems - 1, firstVisible + FP_VISIBLE_ITEMS + 1);

            for (int idx = firstVisible; idx <= lastVisible; ++idx)
            {
                int relY = idx * FP_ITEM_H - (int)scrollY; // relative to viewport
                if (relY + FP_ITEM_H < 0 || relY > FP_LIST_H)
                    continue;

                // item background (flat) and subtle separator line
                tft.fillRect(0, relY, FP_LIST_W, FP_ITEM_H, BG);
                if (idx > 0)
                    tft.drawFastHLine(8, relY, FP_LIST_W - 16, ACCENT);

                // icon area (flat rounded rect)
                int iconX = ICON_X_OFFSET;
                int iconY = relY + (FP_ITEM_H - 18) / 2;
                if (entriesIsDir[idx])
                {
                    tft.drawRoundRect(iconX, iconY, ICON_WIDTH, 14, 4, PRIMARY);
                    // small tab for folder
                    tft.drawFastHLine(iconX + 3, iconY - 4, 10, PRIMARY);
                }
                else
                {
                    tft.drawRoundRect(iconX + 4, iconY, ICON_WIDTH - 8, 14, 3, ACCENT);
                }

                // text (truncate if needed)
                tft.setTextSize(1);
                tft.setTextColor(TEXT, BG);
                tft.setTextDatum(TL_DATUM);
                int textX = iconX + ICON_WIDTH + 8;
                tft.drawString(entries[idx], textX, relY + 12, 1);

                // chevron for dirs (right aligned)
                if (entriesIsDir[idx])
                {
                    int cx = FP_LIST_W - 16;
                    int cy = relY + (FP_ITEM_H / 2) - 6;
                    tft.fillTriangle(cx, cy, cx + CHEVRON_W, cy + 6, cx, cy + 12, TEXT);
                }
            }

            // unset viewport; restore full screen viewport
            tft.setViewport(0, 0, FP_SCREEN_W, FP_SCREEN_H, true);
        };

        // initial draw
        redrawList();

        // Touch interaction state (robust)
        bool touching = false;
        float touchStartX = 0.0f, touchStartY = 0.0f;
        float lastTouchY = 0.0f;
        unsigned long touchStartTime = 0;
        unsigned long lastTouchTime = 0;

        // helper to clamp scroll
        auto clampScroll = [&](void)
        {
            if (scrollY < 0)
                scrollY = 0;
            if (scrollY > maxScroll)
                scrollY = maxScroll;
        };

        // helper to recompute content metrics (call after readdir)
        auto recomputeContent = [&]()
        {
            totalItems = (int)entries.size();
            contentHeight = totalItems * FP_ITEM_H;
            viewportHeight = FP_LIST_H;
            maxScroll = std::max(0, contentHeight - viewportHeight);
            totalPages = std::max(1, (int)((entries.size() + FP_VISIBLE_ITEMS - 1) / FP_VISIBLE_ITEMS));
        };

        // Main event loop (blocking): returns on selection or cancel
        while (true)
        {
            Screen::TouchPos ti = Screen::getTouchPos(); // struct with clicked, pos, move

            if (ti.clicked)
            {
                // pressed or moving
                if (!touching)
                {
                    // touch start
                    touching = true;
                    touchStartX = ti.x;
                    touchStartY = ti.y;
                    lastTouchY = ti.y;
                    touchStartTime = lastTouchTime = millis();
                    velocity = 0.0f;
                }
                else
                {
                    // dragging: use movement delta
                    unsigned long now = millis();
                    float dt = (now - lastTouchTime) / 1000.0f;
                    float delta = ti.y - lastTouchY; // finger movement since last sample
                    // move content opposite to finger
                    scrollY -= delta;
                    // soft overscroll
                    if (scrollY < -MAX_OVERSCROLL)
                        scrollY = -MAX_OVERSCROLL;
                    if (scrollY > maxScroll + MAX_OVERSCROLL)
                        scrollY = maxScroll + MAX_OVERSCROLL;

                    // compute velocity
                    if (dt > 0.0f)
                    {
                        velocity = (delta) / dt; // finger px/sec
                        lastTouchTime = now;
                    }
                    lastTouchY = ti.y;
                }

                // immediate visual feedback while dragging
                redrawList();
            }
            else
            {
                // released
                if (touching)
                {
                    unsigned long now = millis();
                    unsigned long totalTime = now - touchStartTime;
                    float totalMove = fabs(lastTouchY - touchStartY);

                    // detect tap (short time + small move)
                    bool isTap = (totalMove <= TAP_THRESHOLD && totalTime <= TAP_MAX_TIME);

                    // use stored start coords for tap target
                    int px = (int)touchStartX;
                    int py = (int)touchStartY;

                    if (isTap)
                    {
                        // Header back area (left of header bar inside header rect)
                        Rect headerRect{Vec{0, FP_HEADER_TOP}, Vec{FP_SCREEN_W, FP_HEADER_H}};
                        if (headerRect.isIn(Vec{px, py}))
                        {
                            Rect backRect{Vec{OUTER_PAD, FP_HEADER_TOP}, Vec{36, FP_HEADER_H}};
                            if (backRect.isIn(Vec{px, py}))
                            {
                                if (!curPath.empty())
                                {
                                    curPath.pop_back();
                                    curPathStr = pathToString(curPath);
                                    readEntries(curPath, entries, entriesIsDir);
                                    recomputeContent();
                                    scrollY = 0;
                                    page = 0;
                                    drawHeader(curPathStr);
                                    drawFooter(page, totalPages);
                                    redrawList();
                                    touching = false;
                                    velocity = 0;
                                    delay(80);
                                    continue;
                                }
                            }
                        }

                        // Footer Cancel / Select areas (exact positions)
                        int fy = FP_FOOTER_TOP;
                        int btnW = 92;
                        int btnH = FP_FOOTER_H - 16;
                        int bx = OUTER_PAD + 12;
                        int by = fy + 8;
                        int sx = FP_SCREEN_W - OUTER_PAD - 12 - btnW;
                        Rect cancelRect{Vec{bx, by}, Vec{btnW, btnH}};
                        Rect selectRect{Vec{sx, by}, Vec{btnW, btnH}};
                        if (cancelRect.isIn(Vec{px, py}))
                        {
                            while (Screen::getTouchPos().clicked)
                                delay(5);
                            return String("");
                        }
                        if (selectRect.isIn(Vec{px, py}))
                        {
                            while (Screen::getTouchPos().clicked)
                                delay(5);
                            return curPathStr;
                        }

                        // Page left/right areas (center) - keep small hit zones
                        Rect pageLeft{Vec{(FP_SCREEN_W / 2) - 50, fy + 8}, Vec{40, btnH}};
                        Rect pageRight{Vec{(FP_SCREEN_W / 2) + 10, fy + 8}, Vec{40, btnH}};
                        if (pageLeft.isIn(Vec{px, py}))
                        {
                            if (page > 0)
                            {
                                page--;
                                scrollY = page * FP_VISIBLE_ITEMS * FP_ITEM_H;
                                clampScroll();
                                redrawList();
                                drawFooter(page, totalPages);
                                delay(80);
                            }
                            touching = false;
                            continue;
                        }
                        if (pageRight.isIn(Vec{px, py}))
                        {
                            if (page + 1 < totalPages)
                            {
                                page++;
                                scrollY = page * FP_VISIBLE_ITEMS * FP_ITEM_H;
                                clampScroll();
                                redrawList();
                                drawFooter(page, totalPages);
                                delay(80);
                            }
                            touching = false;
                            continue;
                        }

                        // Check if tap inside list selectable region
                        Rect listRect{Vec{FP_LIST_X, FP_LIST_Y}, Vec{FP_LIST_W, FP_LIST_H}};
                        if (listRect.isIn(Vec{px, py}))
                        {
                            int relY = (int)(py - FP_LIST_Y + (int)scrollY);
                            int itemIndex = relY / FP_ITEM_H;
                            int globalIndex = itemIndex;
                            if (globalIndex >= 0 && globalIndex < (int)entries.size())
                            {
                                // pressed visual: rounded overlay within list; no shadows so no overlap issues
                                int y = FP_LIST_Y + (globalIndex * FP_ITEM_H - (int)scrollY);
                                // draw within global coords since viewport is off here
                                tft.fillRoundRect(FP_LIST_X + 4, y + 4, FP_LIST_W - 8, FP_ITEM_H - 8, ITEM_RADIUS, ACCENT);
                                delay(100);
                                if (entriesIsDir[globalIndex])
                                {
                                    curPath.push_back(entries[globalIndex]);
                                    curPathStr = pathToString(curPath);
                                    readEntries(curPath, entries, entriesIsDir);
                                    recomputeContent();
                                    scrollY = 0;
                                    page = 0;
                                    drawHeader(curPathStr);
                                    drawFooter(page, totalPages);
                                    redrawList();
                                    touching = false;
                                    velocity = 0;
                                    continue;
                                }
                                else
                                {
                                    Path chosen = curPath;
                                    chosen.push_back(entries[globalIndex]);
                                    String chosenStr = pathToString(chosen);
                                    while (Screen::getTouchPos().clicked)
                                        delay(5);
                                    return chosenStr;
                                }
                            }
                        }
                    } // end isTap

                    // if not a tap -> let inertia handle it
                    touching = false;
                }

                // decelerate / inertia when not touching
                if (!touching)
                {
                    if (fabs(velocity) > 200.0f)
                    {
                        float dt = 0.016f;
                        float fingerDelta = velocity * dt;
                        scrollY -= fingerDelta;
                        velocity *= SCROLL_FRICTION;
                        if (scrollY < 0)
                        {
                            scrollY = 0;
                            velocity = 0;
                        }
                        if (scrollY > maxScroll)
                        {
                            scrollY = maxScroll;
                            velocity = 0;
                        }
                        redrawList();
                    }
                    else
                    {
                        velocity = 0;
                        clampScroll();
                    }
                }
            }

            // update page when stationary
            if (!touching && fabs(velocity) < 200.0f)
            {
                if (FP_VISIBLE_ITEMS > 0)
                {
                    page = (int)round(scrollY / (FP_VISIBLE_ITEMS * FP_ITEM_H));
                    page = std::max(0, std::min(page, totalPages - 1));
                }
            }

            delay(16);
        }

        // fallback
        tft.fillScreen(BG);
        return String("");
    }
} // namespace FilePicker

// Public API
static inline String filePicker(String path = "/")
{
    return FilePicker::filePickerImpl(path);
}
