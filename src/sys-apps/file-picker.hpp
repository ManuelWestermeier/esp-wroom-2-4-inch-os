#pragma once
// file: file-picker.hpp
// A simple white file/folder picker UI for 320x240 TFT using ENC_FS and Screen
// Returns the selected plain path as "/foo/bar.txt" or empty String if cancelled.

#include <Arduino.h>
#include <vector>
#include <algorithm>

#include "../fs/enc-fs.hpp"    // provides ENC_FS::*
#include "../screen/index.hpp" // provides Screen::tft and Screen::getTouchPos()
#include "../utils/vec.hpp"
#include "../utils/rect.hpp"

using std::vector;

namespace FilePicker
{
    using Path = ENC_FS::Path;

    // ---- Layout constants (prefixed to avoid macro collisions) ----
    static constexpr int FP_SCREEN_W = 320;
    static constexpr int FP_SCREEN_H = 240;

    static constexpr int FP_HEADER_H = 28;
    static constexpr int FP_FOOTER_H = 36;
    static constexpr int FP_LIST_Y = FP_HEADER_H;
    static constexpr int FP_ITEM_H = 20;
    static constexpr int FP_LIST_H = (FP_SCREEN_H - FP_HEADER_H - FP_FOOTER_H);
    static constexpr int FP_VISIBLE_ITEMS = (FP_LIST_H / FP_ITEM_H);

    // Colors (TFT_eSPI constants / 16-bit)
    static const uint16_t FP_COL_BG = TFT_WHITE;
    static const uint16_t FP_COL_TEXT = TFT_BLACK;
    static const uint16_t FP_COL_HIGHLIGHT = 0xC618; // light gray-ish
    static const uint16_t FP_COL_BUTTON = 0xF7DE;    // slightly off-white
    static const uint16_t FP_COL_GRAY = 0x8410;

    // ---- Helpers ----
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
        return ENC_FS::str2Path(s);
    }

    // draw a simple back arrow in header
    static void drawBackArrow(int x, int y, int size = 10)
    {
        // fill triangle (x1,y1,x2,y2,x3,y3,color)
        Screen::tft.fillTriangle(x, y, x + size, y + size / 2, x, y + size, FP_COL_TEXT);
    }

    // Draw the static chrome: header, footer buttons
    static void drawChrome(const String &currentPathStr, int pageIndex, int totalPages)
    {
        auto &tft = Screen::tft;
        // background
        tft.fillScreen(FP_COL_BG);

        // Header bar
        tft.fillRect(0, 0, FP_SCREEN_W, FP_HEADER_H, FP_COL_BUTTON);
        tft.drawRect(0, 0, FP_SCREEN_W, FP_HEADER_H, FP_COL_GRAY);

        // Back arrow (top-left)
        drawBackArrow(6, (FP_HEADER_H - 10) / 2);

        // Current path text
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(1);
        tft.setTextColor(FP_COL_TEXT, FP_COL_BUTTON);
        tft.drawString(currentPathStr, 20, 6, 2);

        // Footer
        tft.fillRect(0, FP_SCREEN_H - FP_FOOTER_H, FP_SCREEN_W, FP_FOOTER_H, FP_COL_BUTTON);
        tft.drawRect(0, FP_SCREEN_H - FP_FOOTER_H, FP_SCREEN_W, FP_FOOTER_H, FP_COL_GRAY);

        // Cancel button
        tft.drawRect(6, FP_SCREEN_H - FP_FOOTER_H + 6, 70, FP_FOOTER_H - 12, FP_COL_GRAY);
        tft.setTextColor(FP_COL_TEXT, FP_COL_BUTTON);
        tft.drawString("Cancel", 12, FP_SCREEN_H - FP_FOOTER_H + 10, 2);

        // Page indicator (use std::max to avoid macro clash)
        String pstr = String(pageIndex + 1) + "/" + String(std::max(1, totalPages));
        tft.setTextColor(FP_COL_TEXT, FP_COL_BUTTON);
        tft.drawString(pstr, (FP_SCREEN_W / 2) - 10, FP_SCREEN_H - FP_FOOTER_H + 10, 2);

        // Select button
        tft.drawRect(FP_SCREEN_W - 78, FP_SCREEN_H - FP_FOOTER_H + 6, 70, FP_FOOTER_H - 12, FP_COL_GRAY);
        tft.setTextColor(FP_COL_TEXT, FP_COL_BUTTON);
        tft.drawString("Select", FP_SCREEN_W - 70, FP_SCREEN_H - FP_FOOTER_H + 10, 2);
    }

    // Draw a single item at index (0..FP_VISIBLE_ITEMS-1)
    static void drawItem(int idxOnPage, const String &name, bool isDir, bool pressed = false)
    {
        auto &tft = Screen::tft;
        int x = 0;
        int y = FP_LIST_Y + idxOnPage * FP_ITEM_H;
        int w = FP_SCREEN_W;
        int h = FP_ITEM_H;

        // background for item
        if (pressed)
            tft.fillRect(x, y, w, h, FP_COL_HIGHLIGHT);
        else
            tft.fillRect(x, y, w, h, FP_COL_BG);

        // separator line
        tft.drawFastHLine(x, y + h - 1, w, FP_COL_GRAY);

        // icon
        if (isDir)
        {
            tft.fillRect(6, y + 4, 14, 12, FP_COL_GRAY);
            tft.fillRect(8, y + 2, 10, 6, FP_COL_GRAY);
        }
        else
        {
            tft.drawRect(8, y + 3, 12, 14, FP_COL_GRAY);
        }

        // text
        tft.setTextColor(FP_COL_TEXT, FP_COL_BG);
        tft.setTextSize(1);
        tft.drawString(name, 28, y + 3, 1);
    }

    // Read directory entries (plain names) and metadata flag vector (isDir)
    static void readEntries(const Path &dirPath, vector<String> &names, vector<bool> &isDir)
    {
        names.clear();
        isDir.clear();
        vector<String> n = ENC_FS::readDir(dirPath);

        // sort lexicographically (case-insensitive)
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
    }

    // The main file picker function (exposed)
    static String filePickerImpl(String startPath = "/")
    {
        Path curPath = stringToPath(startPath);
        String curPathStr = pathToString(curPath);

        vector<String> entries;
        vector<bool> entriesIsDir;
        readEntries(curPath, entries, entriesIsDir);

        int page = 0;
        int totalPages = std::max(1, (int)((entries.size() + FP_VISIBLE_ITEMS - 1) / FP_VISIBLE_ITEMS));

        // draw initial chrome and list
        drawChrome(curPathStr, page, totalPages);

        // draw list page lambda
        auto redrawPage = [&](int pageIndex)
        {
            // clear list area
            Screen::tft.fillRect(0, FP_LIST_Y, FP_SCREEN_W, FP_LIST_H, FP_COL_BG);

            int startIdx = pageIndex * FP_VISIBLE_ITEMS;
            for (int i = 0; i < FP_VISIBLE_ITEMS; ++i)
            {
                int idx = startIdx + i;
                if (idx >= (int)entries.size())
                {
                    Screen::tft.fillRect(0, FP_LIST_Y + i * FP_ITEM_H, FP_SCREEN_W, FP_ITEM_H, FP_COL_BG);
                    continue;
                }
                drawItem(i, entries[idx], entriesIsDir[idx], false);
            }

            // redraw chrome (page numbers may have changed)
            drawChrome(curPathStr, pageIndex, totalPages);
        };

        redrawPage(page);

        // Main event loop - blocking until user selects file or cancels
        while (true)
        {
            Screen::TouchPos t = Screen::getTouchPos();
            if (!t.clicked)
            {
                delay(10);
                continue;
            }

            int tx = t.x;
            int ty = t.y;

            // Header back arrow area
            Rect backRect{Vec{0, 0}, Vec{28, FP_HEADER_H}};
            if (backRect.isIn(t))
            {
                if (!curPath.empty())
                {
                    curPath.pop_back();
                    curPathStr = pathToString(curPath);
                    readEntries(curPath, entries, entriesIsDir);
                    page = 0;
                    totalPages = std::max(1, (int)((entries.size() + FP_VISIBLE_ITEMS - 1) / FP_VISIBLE_ITEMS));
                    redrawPage(page);
                }
                while (Screen::getTouchPos().clicked)
                    delay(5);
                continue;
            }

            // Footer Cancel button area
            Rect cancelRect{Vec{6, FP_SCREEN_H - FP_FOOTER_H + 6}, Vec{70, FP_FOOTER_H - 12}};
            if (cancelRect.isIn(t))
            {
                while (Screen::getTouchPos().clicked)
                    delay(5);
                return String(""); // cancelled
            }

            // Footer Select button area
            Rect selectRect{Vec{FP_SCREEN_W - 78, FP_SCREEN_H - FP_FOOTER_H + 6}, Vec{70, FP_FOOTER_H - 12}};
            if (selectRect.isIn(t))
            {
                while (Screen::getTouchPos().clicked)
                    delay(5);
                return curPathStr; // return current folder path
            }

            // Page left/right areas
            Rect pageLeft{Vec{(FP_SCREEN_W / 2) - 50, FP_SCREEN_H - FP_FOOTER_H + 6}, Vec{40, FP_FOOTER_H - 12}};
            Rect pageRight{Vec{(FP_SCREEN_W / 2) + 10, FP_SCREEN_H - FP_FOOTER_H + 6}, Vec{40, FP_FOOTER_H - 12}};
            if (pageLeft.isIn(t))
            {
                if (page > 0)
                {
                    page--;
                    redrawPage(page);
                }
                while (Screen::getTouchPos().clicked)
                    delay(5);
                continue;
            }
            if (pageRight.isIn(t))
            {
                if (page + 1 < totalPages)
                {
                    page++;
                    redrawPage(page);
                }
                while (Screen::getTouchPos().clicked)
                    delay(5);
                continue;
            }

            // check if tapped inside list area
            Rect listRect{Vec{0, FP_LIST_Y}, Vec{FP_SCREEN_W, FP_LIST_H}};
            if (listRect.isIn(t))
            {
                int relY = ty - FP_LIST_Y;
                int itemIndex = relY / FP_ITEM_H;
                int globalIndex = page * FP_VISIBLE_ITEMS + itemIndex;
                if (globalIndex >= 0 && globalIndex < (int)entries.size())
                {
                    // pressed visual
                    drawItem(itemIndex, entries[globalIndex], entriesIsDir[globalIndex], true);

                    // wait until release
                    while (Screen::getTouchPos().clicked)
                        delay(5);

                    if (entriesIsDir[globalIndex])
                    {
                        curPath.push_back(entries[globalIndex]);
                        curPathStr = pathToString(curPath);
                        readEntries(curPath, entries, entriesIsDir);
                        page = 0;
                        totalPages = std::max(1, (int)((entries.size() + FP_VISIBLE_ITEMS - 1) / FP_VISIBLE_ITEMS));
                        redrawPage(page);
                        continue;
                    }
                    else
                    {
                        Path chosen = curPath;
                        chosen.push_back(entries[globalIndex]);
                        String chosenStr = pathToString(chosen);
                        return chosenStr;
                    }
                }
            }

            // debounce
            delay(10);
        }

        return String("");
    }
} // namespace FilePicker

// Public API
static inline String filePicker(String path = "/")
{
    return FilePicker::filePickerImpl(path);
}
