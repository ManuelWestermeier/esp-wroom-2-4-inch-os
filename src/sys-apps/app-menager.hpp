#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
#include <vector>
#include <algorithm>

#include "../io/read-string.hpp"
#include "../screen/index.hpp"
#include "../fs/enc-fs.hpp"
#include "../styles/global.hpp"

namespace AppManager
{

    struct Buffer
    {
        std::vector<uint8_t> data;
        bool ok = false;
    };

    // ---------- font sizes ----------
    // Adjusted for better visibility on 320x240 display
    static const int TITLE_FONT = 2;   // large title (reduced from 4)
    static const int HEADING_FONT = 2; // headings / button labels
    static const int BODY_FONT = 1;    // body text
    static const int BUTTON_FONT = 1;  // button text (smaller for better fit)
    static const int DEFAULT_FONT = BODY_FONT;

    // Layout margins (use free space safely)
    static const int LEFT_MARGIN = 8;
    static const int RIGHT_MARGIN = 8;
    static const int TOP_MARGIN = 8;
    static const int BOTTOM_MARGIN = 8;

    // ---------- helpers ----------

    String trimLines(const String &s)
    {
        int start = 0;
        while (start < (int)s.length() && isspace((unsigned char)s[start]))
            start++;
        int end = s.length() - 1;
        while (end >= 0 && isspace((unsigned char)s[end]))
            end--;
        if (end < start)
            return "";
        return s.substring(start, end + 1);
    }

    static String sanitizeFolderName(const String &s)
    {
        String out;
        out.reserve(s.length());
        for (size_t i = 0; i < s.length(); ++i)
        {
            char c = s[i];
            if ((c >= 'a' && c <= 'z') ||
                (c >= 'A' && c <= 'Z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '_')
                out += c;
            else
                out += '_';
        }
        if (out.length() == 0)
            out = "app";
        return out;
    }

    // Clipped string with ellipsis — font selectable
    static void drawClippedString(int x, int y, int maxW, const String &s, int font = DEFAULT_FONT)
    {
        if (s.length() == 0)
            return;
        // enforce left margin so text never starts outside viewport
        x = std::max(x, LEFT_MARGIN);
        const char *cs = s.c_str();
        int w = Screen::tft.textWidth(cs, font);
        if (w <= maxW)
        {
            Screen::tft.drawString(s, x, y, font);
            return;
        }

        String ellipsis = "...";
        int ellipsisWidth = Screen::tft.textWidth(ellipsis.c_str(), font);

        String displayText = s;
        while (displayText.length() > 0 &&
               Screen::tft.textWidth(displayText.c_str(), font) + ellipsisWidth > maxW)
        {
            displayText.remove(displayText.length() - 1);
        }

        displayText += ellipsis;
        Screen::tft.drawString(displayText, x, y, font);
    }

    // ---------- UI helpers (320x240 friendly) ----------

    static int screenW()
    {
        return Screen::tft.width();
    }
    static int screenH()
    {
        return Screen::tft.height();
    }

    static void clearScreen(uint16_t color = BG)
    {
        Screen::tft.fillScreen(color);
    }

    static void drawTitle(const String &title)
    {
        Screen::tft.setTextColor(TEXT, BG);
        int font = TITLE_FONT;
        int w = Screen::tft.textWidth(title.c_str(), font);
        int x = (screenW() - w) / 2;
        x = std::max(x, LEFT_MARGIN);
        x = std::min(x, screenW() - w - RIGHT_MARGIN); // Ensure title doesn't go off right edge
        int y = TOP_MARGIN;
        Screen::tft.drawString(title, x, y, font);
    }

    static void drawMessage(const String &msg, int y = 36, uint16_t fg = TEXT, uint16_t bg = BG, int font = BODY_FONT)
    {
        Screen::tft.setTextColor(fg, bg);
        int maxW = screenW() - LEFT_MARGIN - RIGHT_MARGIN;
        drawClippedString(LEFT_MARGIN, y, maxW, msg, font);
    }

    struct BtnRect
    {
        int x, y, w, h;
        bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    };

    // Improved button drawing with better text handling
    static void drawButton(const BtnRect &r, const char *label, uint16_t bg = PRIMARY, uint16_t fg = AT, int font = BUTTON_FONT)
    {
        int radius = std::min(8, r.h / 4); // Smaller radius for smaller buttons
        Screen::tft.fillRoundRect(r.x, r.y, r.w, r.h, radius, bg);
        Screen::tft.drawRoundRect(r.x, r.y, r.w, r.h, radius, ACCENT2);
        Screen::tft.setTextColor(fg, bg);

        // Calculate text dimensions
        int textW = Screen::tft.textWidth(label, font);
        int textH = Screen::tft.fontHeight(font);

        // Center text in button
        int tx = r.x + (r.w - textW) / 2;
        int ty = r.y + (r.h - textH) / 2;

        // Ensure text stays within button boundaries
        if (tx < r.x + 4)
            tx = r.x + 4;
        if (tx + textW > r.x + r.w)
            tx = r.x + r.w - textW - 2;
        if (ty < r.y)
            ty = r.y;
        if (ty + textH > r.y + r.h)
            ty = r.y + r.h - textH;

        Screen::tft.drawString(label, tx, ty, font);
    }

    static void drawError(const String &msg)
    {
        clearScreen(DANGER);
        Screen::tft.setTextColor(AT, DANGER);
        Screen::tft.drawString("Error:", LEFT_MARGIN, TOP_MARGIN, HEADING_FONT);
        drawClippedString(LEFT_MARGIN, TOP_MARGIN + Screen::tft.fontHeight(HEADING_FONT) + 4,
                          screenW() - LEFT_MARGIN - RIGHT_MARGIN, msg, BODY_FONT);
        Serial.println("[ERROR] " + msg);
        delay(2000);
    }

    static void drawSuccess(const String &msg)
    {
        clearScreen(PRIMARY);
        Screen::tft.setTextColor(AT, PRIMARY);
        int maxW = screenW() - LEFT_MARGIN - RIGHT_MARGIN;
        int y = (screenH() - Screen::tft.fontHeight(HEADING_FONT)) / 2;
        drawClippedString(LEFT_MARGIN, y, maxW, msg, HEADING_FONT);
        Serial.println("[SUCCESS] " + msg);
        delay(1500);
    }

    static void drawProgressBar(int x, int y, int width, int height, int progress, uint16_t color = PRIMARY)
    {
        x = std::max(x, LEFT_MARGIN);
        width = std::min(width, screenW() - x - RIGHT_MARGIN);
        Screen::tft.drawRoundRect(x, y, width, height, 4, ACCENT2);

        int innerW = width - 4;
        int fillWidth = (progress * innerW) / 100;

        if (fillWidth > 0)
        {
            Screen::tft.fillRoundRect(x + 2, y + 2, fillWidth, height - 4, 2, color);
        }

        String percent = String(progress) + "%";
        Screen::tft.setTextColor(TEXT, BG);
        int textW = Screen::tft.textWidth(percent.c_str(), BODY_FONT);
        int tx = x + (width - textW) / 2;
        tx = std::max(tx, x + 2);
        tx = std::min(tx, x + width - textW - 2);
        int ty = y + (height - Screen::tft.fontHeight(BODY_FONT)) / 2;
        Screen::tft.drawString(percent, tx, ty, BODY_FONT);
    }

    // ---------- networking ----------

    static WiFiClientSecure secureClient; // global reusable

    // Read an HTTP(S) stream reliably in small chunks, with retries for tiny files.
    static bool performGet(const String &url, Buffer &outBuf)
    {
        outBuf.data.clear();
        outBuf.ok = false;
        Serial.println("[GET] URL: " + url);

        if (WiFi.status() != WL_CONNECTED)
        {
            drawError("WiFi not connected");
            return false;
        }

        HTTPClient http;
        secureClient.setInsecure();
        if (!http.begin(secureClient, url))
        {
            drawError("http.begin failed");
            return false;
        }

        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(15000);

        int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            Serial.printf("[ERROR] HTTP GET failed: %d\n", code);
            http.end();
            return false;
        }

        WiFiClient &stream = http.getStream();
        outBuf.data.clear();
        const size_t BUF_SZ = 256;
        uint8_t tmp[BUF_SZ];
        unsigned long start = millis();

        while (stream.connected() || stream.available())
        {
            while (stream.available())
            {
                int r = stream.read(tmp, BUF_SZ);
                if (r > 0)
                {
                    outBuf.data.insert(outBuf.data.end(), tmp, tmp + r);
                    start = millis();
                }
                else if (r < 0)
                {
                    Serial.println("[ERROR] Stream read error");
                    http.end();
                    return false;
                }
            }
            if (millis() - start > 5000)
                break;
            delay(1);
        }

        outBuf.ok = true;
        http.end();
        return true;
    }

    static bool performGetWithFallback(const String &url, Buffer &buf)
    {
        return performGet(url, buf);
    }

    static bool fetchAndWrite(const String &url, const String &path, const String &folderName, bool required, int &progress, int totalFiles, int currentFile)
    {
        Serial.println("[Download] " + url + " -> " + path);

        // Update progress UI using the freed vertical space
        progress = (currentFile * 100) / totalFiles;
        clearScreen();
        // draw title directly (no wrapper)
        Screen::tft.setTextColor(TEXT, BG);
        {
            const String __t = String("Installing App");
            int __font = TITLE_FONT;
            int __w = Screen::tft.textWidth(__t.c_str(), __font);
            int __x = (screenW() - __w) / 2;
            __x = std::max(__x, LEFT_MARGIN);
            __x = std::min(__x, screenW() - __w - RIGHT_MARGIN);
            int __y = TOP_MARGIN;
            Screen::tft.drawString(__t, __x, __y, __font);
        }
        drawMessage("Downloading files...", TOP_MARGIN + Screen::tft.fontHeight(TITLE_FONT) + 8, TEXT, BG, HEADING_FONT);

        // Progress bar position adjusted for smaller screen
        int pbX = LEFT_MARGIN;
        int pbW = screenW() - LEFT_MARGIN - RIGHT_MARGIN;
        int pbY = screenH() / 2 - 14;
        drawProgressBar(pbX, pbY, pbW, 24, progress);
        drawClippedString(pbX, pbY + 30, pbW, "Downloading: " + path, BODY_FONT);

        Buffer dataBuf;
        if (!performGetWithFallback(url, dataBuf))
        {
            if (required)
                drawError("Failed to download " + path);
            return !required;
        }

        if (!ENC_FS::exists({"programs"}))
            ENC_FS::mkDir({"programs"});
        if (!ENC_FS::exists({"programs", folderName}))
            ENC_FS::mkDir({"programs", folderName});

        bool written = ENC_FS::writeFile({"programs", folderName, path}, 0, 0, dataBuf.data);
        Serial.println("[Write] File " + path + (written ? " OK" : " FAILED"));

        if (!written && required)
            drawError("Failed to save " + path);
        return written;
    }

    static std::vector<String> parsePkgTxt(const Buffer &buf)
    {
        std::vector<String> out;
        if (!buf.ok)
            return out;
        String s((const char *)buf.data.data(), buf.data.size());
        int idx = 0;
        while (idx < (int)s.length())
        {
            int nl = s.indexOf('\n', idx);
            String line;
            if (nl == -1)
            {
                line = s.substring(idx);
                idx = s.length();
            }
            else
            {
                line = s.substring(idx, nl);
                idx = nl + 1;
            }
            line = trimLines(line);
            if (line.length() > 0)
                out.push_back(line);
        }
        return out;
    }

    static void safePush20x20Icon(int x, int y, const Buffer &buf, int scale = 1)
    {
        // Support scaling (scale==2 -> 40x40)
        const int SRC_W = 20;
        const int SRC_H = 20;
        const size_t SRC_PIX = SRC_W * SRC_H;
        if (!buf.ok || buf.data.size() < 4)
            return;
        const uint8_t *p = buf.data.data() + 4;
        // extract source pixels
        std::vector<uint16_t> src(SRC_PIX);
        for (size_t i = 0; i < SRC_PIX; ++i)
        {
            size_t off = i * 2;
            if (off + 1 < buf.data.size() - 4)
                src[i] = (uint16_t)p[off] | ((uint16_t)p[off + 1] << 8);
            else
                src[i] = 0;
        }
        if (scale <= 1)
        {
            Screen::tft.pushImage(x, y, SRC_W, SRC_H, src.data());
            return;
        }
        int dstW = SRC_W * scale;
        int dstH = SRC_H * scale;
        std::vector<uint16_t> dst((size_t)dstW * dstH);
        for (int sy = 0; sy < SRC_H; ++sy)
        {
            for (int sx = 0; sx < SRC_W; ++sx)
            {
                uint16_t col = src[sy * SRC_W + sx];
                // replicate into scale x scale block
                for (int dy = 0; dy < scale; ++dy)
                {
                    for (int dx = 0; dx < scale; ++dx)
                    {
                        int dxPos = sx * scale + dx;
                        int dyPos = sy * scale + dy;
                        dst[dyPos * dstW + dxPos] = col;
                    }
                }
            }
        }
        Screen::tft.pushImage(x, y, dstW, dstH, dst.data());
    }

    // ---------- user interaction ----------

    static char waitForTwoButtonChoice(const BtnRect &a, const BtnRect &b)
    {
        unsigned long start = millis();
        while (true)
        {
            Screen::TouchPos tp = Screen::getTouchPos();
            if (tp.clicked)
            {
                int tx = (int)tp.x, ty = (int)tp.y;
                if (a.contains(tx, ty))
                    return 'i';
                if (b.contains(tx, ty))
                    return 'c';
            }
            if (Serial.available())
            {
                String s = readString("");
                s.trim();
                if (s.length() > 0)
                {
                    char c = tolower(s[0]);
                    if (c == 'i' || c == 'c')
                        return c;
                }
            }
            if (millis() - start > 120000)
                return 'c';
            delay(10);
        }
    }

    static bool confirmInstallPrompt(const String &appName, const Buffer &iconBuf, const String &version)
    {
        clearScreen();
        // draw title directly (no wrapper)
        Screen::tft.setTextColor(TEXT, BG);
        {
            const String __t = String("Install App?");
            int __font = TITLE_FONT;
            int __w = Screen::tft.textWidth(__t.c_str(), __font);
            int __x = (screenW() - __w) / 2;
            __x = std::max(__x, LEFT_MARGIN);
            __x = std::min(__x, screenW() - __w - RIGHT_MARGIN);
            int __y = TOP_MARGIN;
            Screen::tft.drawString(__t, __x, __y, __font);
        }

        // Render scaled icon on the RIGHT with scale factor 2 (40x40)
        int iconScale = 2;
        int iconW = 20 * iconScale;
        int iconH = 20 * iconScale;
        int iconX = screenW() - RIGHT_MARGIN - iconW;
        int iconY = TOP_MARGIN + Screen::tft.fontHeight(TITLE_FONT) + 12;
        safePush20x20Icon(iconX, iconY, iconBuf, iconScale);

        // Text area to the LEFT of the icon
        int textX = LEFT_MARGIN;
        int textW = iconX - textX - 8;
        int nameY = iconY; // align name with top of icon
        // Make name larger for emphasis
        drawClippedString(textX, nameY, textW, "Name: " + trimLines(appName), HEADING_FONT);
        drawClippedString(textX, nameY + Screen::tft.fontHeight(HEADING_FONT) + 4, textW, "Version: " + trimLines(version), BODY_FONT);
        drawClippedString(textX, nameY + Screen::tft.fontHeight(HEADING_FONT) + Screen::tft.fontHeight(BODY_FONT) + 8, textW, "Install this app to /programs/" + sanitizeFolderName(appName) + "?", BODY_FONT);

        // Buttons centered and sized for touch; use wider buttons to use free space
        int btnW = (screenW() - 32) / 2;
        int btnY = screenH() - BOTTOM_MARGIN - 48;
        BtnRect yes{LEFT_MARGIN, btnY, btnW, 40};
        BtnRect no{screenW() - RIGHT_MARGIN - btnW, btnY, btnW, 40};

        drawButton(yes, "Install", ACCENT, AT, BUTTON_FONT);
        drawButton(no, "Cancel", DANGER, AT, BUTTON_FONT);

        char c = waitForTwoButtonChoice(yes, no);
        return (c == 'i');
    }

    static bool ensureWiFiConnected(unsigned long timeoutMs = 8000)
    {
        if (WiFi.status() == WL_CONNECTED)
            return true;

        clearScreen();
        // draw title directly (no wrapper)
        Screen::tft.setTextColor(TEXT, BG);
        {
            const String __t = String("Connecting to WiFi");
            int __font = TITLE_FONT;
            int __w = Screen::tft.textWidth(__t.c_str(), __font);
            int __x = (screenW() - __w) / 2;
            __x = std::max(__x, LEFT_MARGIN);
            __x = std::min(__x, screenW() - __w - RIGHT_MARGIN);
            int __y = TOP_MARGIN;
            Screen::tft.drawString(__t, __x, __y, __font);
        }
        drawMessage("Please wait...", TOP_MARGIN + Screen::tft.fontHeight(TITLE_FONT) + 12, TEXT, BG, HEADING_FONT);

        unsigned long start = millis();
        while (millis() - start < timeoutMs)
        {
            if (WiFi.status() == WL_CONNECTED)
                return true;
            delay(100);
        }
        drawError("WiFi not connected");
        return false;
    }

    static bool installApp(const String &rawAppId)
    {
        if (!ensureWiFiConnected(10000))
            return false;

        String base;
        if (!rawAppId.startsWith("http://") && !rawAppId.startsWith("https://"))
            base = "https://" + rawAppId + ".onrender.com/";
        else
            base = rawAppId;

        if (!base.endsWith("/"))
            base += "/";

        Serial.println("[Install] Base URL: " + base);

        String folderName = sanitizeFolderName(rawAppId);

        Buffer nameBuf, verBuf, iconBuf;
        performGetWithFallback(base + "name.txt", nameBuf);
        performGetWithFallback(base + "version.txt", verBuf);
        performGetWithFallback(base + "icon-20x20.raw", iconBuf);

        String name = nameBuf.ok ? String((const char *)nameBuf.data.data(), nameBuf.data.size()) : "Unknown";
        String version = verBuf.ok ? String((const char *)verBuf.data.data(), verBuf.data.size()) : "?";

        Serial.println("[Install] App name: " + name);
        Serial.println("[Install] Version: " + version);

        if (!confirmInstallPrompt(name, iconBuf, version))
            return false;

        // core files
        std::vector<std::pair<String, String>> core = {
            {base + "entry.lua", "entry.lua"},
            {base + "icon-20x20.raw", "icon-20x20.raw"},
            {base + "name.txt", "name.txt"},
            {base + "version.txt", "version.txt"},
        };

        int progress = 0;
        int totalFiles = core.size();
        int currentFile = 0;

        for (auto &p : core)
        {
            currentFile++;
            if (!fetchAndWrite(p.first, p.second, folderName, true, progress, totalFiles, currentFile))
                return false;
        }

        // optional pkg.txt
        Buffer pkg;
        if (performGetWithFallback(base + "pkg.txt", pkg))
        {
            auto extras = parsePkgTxt(pkg);
            totalFiles += extras.size();

            for (auto &f : extras)
            {
                currentFile++;
                if (performGetWithFallback(base + f, pkg))
                {
                    ENC_FS::writeFile({"programs", folderName, f}, 0, 0, pkg.data);
                }
                // Update progress for optional files too
                progress = (currentFile * 100) / totalFiles;
                clearScreen();
                // draw title directly (no wrapper)
                Screen::tft.setTextColor(TEXT, BG);
                {
                    const String __t = String("Installing App");
                    int __font = TITLE_FONT;
                    int __w = Screen::tft.textWidth(__t.c_str(), __font);
                    int __x = (screenW() - __w) / 2;
                    __x = std::max(__x, LEFT_MARGIN);
                    __x = std::min(__x, screenW() - __w - RIGHT_MARGIN);
                    int __y = TOP_MARGIN;
                    Screen::tft.drawString(__t, __x, __y, __font);
                }
                drawMessage("Downloading additional files...", TOP_MARGIN + Screen::tft.fontHeight(TITLE_FONT) + 8, TEXT, BG, HEADING_FONT);
                int pbX = LEFT_MARGIN;
                int pbW = screenW() - LEFT_MARGIN - RIGHT_MARGIN;
                int pbY = screenH() / 2 - 14;
                drawProgressBar(pbX, pbY, pbW, 24, progress);
                drawClippedString(pbX, pbY + 30, pbW, "Downloading: " + f, BODY_FONT);
            }
        }

        // Final progress
        clearScreen();
        // draw title directly (no wrapper)
        Screen::tft.setTextColor(TEXT, BG);
        {
            const String __t = String("Installing App");
            int __font = TITLE_FONT;
            int __w = Screen::tft.textWidth(__t.c_str(), __font);
            int __x = (screenW() - __w) / 2;
            __x = std::max(__x, LEFT_MARGIN);
            __x = std::min(__x, screenW() - __w - RIGHT_MARGIN);
            int __y = TOP_MARGIN;
            Screen::tft.drawString(__t, __x, __y, __font);
        }
        drawMessage("Finalizing installation...", TOP_MARGIN + Screen::tft.fontHeight(TITLE_FONT) + 8, TEXT, BG, HEADING_FONT);
        int pbX = LEFT_MARGIN;
        int pbW = screenW() - LEFT_MARGIN - RIGHT_MARGIN;
        int pbY = screenH() / 2 - 14;
        drawProgressBar(pbX, pbY, pbW, 24, 100);
        delay(500);

        return true;
    }

    static void showInstaller()
    {
        clearScreen();
        // draw title directly (no wrapper)
        Screen::tft.setTextColor(TEXT, BG);
        {
            const String __t = String("App Manager");
            int __font = TITLE_FONT;
            int __w = Screen::tft.textWidth(__t.c_str(), __font);
            int __x = (screenW() - __w) / 2;
            __x = std::max(__x, LEFT_MARGIN);
            __x = std::min(__x, screenW() - __w - RIGHT_MARGIN);
            int __y = TOP_MARGIN;
            Screen::tft.drawString(__t, __x, __y, __font);
        }

        // Use appropriately sized buttons for 320x240 screen
        int btnW = screenW() - LEFT_MARGIN - RIGHT_MARGIN;
        int btnH = 36;
        int btnSpacing = 12;
        int firstBtnY = TOP_MARGIN + Screen::tft.fontHeight(TITLE_FONT) + 20;

        BtnRect installRect{LEFT_MARGIN, firstBtnY, btnW, btnH};
        BtnRect cancelRect{LEFT_MARGIN, firstBtnY + btnH + btnSpacing, btnW, btnH};

        drawButton(installRect, "Install new app", ACCENT3, AT, BUTTON_FONT);
        drawButton(cancelRect, "Cancel", DANGER, AT, BUTTON_FONT);

        char choice = waitForTwoButtonChoice(installRect, cancelRect);
        if (choice != 'i')
            return;

        clearScreen();
        // draw title directly (no wrapper)
        Screen::tft.setTextColor(TEXT, BG);
        {
            const String __t = String("Enter App ID");
            int __font = TITLE_FONT;
            int __w = Screen::tft.textWidth(__t.c_str(), __font);
            int __x = (screenW() - __w) / 2;
            __x = std::max(__x, LEFT_MARGIN);
            __x = std::min(__x, screenW() - __w - RIGHT_MARGIN);
            int __y = TOP_MARGIN;
            Screen::tft.drawString(__t, __x, __y, __font);
        }
        drawMessage("Please enter the App ID", TOP_MARGIN + Screen::tft.fontHeight(TITLE_FONT) + 8, TEXT, BG, HEADING_FONT);
        drawMessage("on the serial monitor", TOP_MARGIN + Screen::tft.fontHeight(TITLE_FONT) + Screen::tft.fontHeight(HEADING_FONT) + 12, TEXT, BG, BODY_FONT);

        String appId = readString("App ID: ");
        appId.trim();
        if (appId.length() == 0)
        {
            drawError("No App ID entered");
            return;
        }

        clearScreen();
        // draw title directly (no wrapper)
        Screen::tft.setTextColor(TEXT, BG);
        {
            const String __t = String("Preparing Installation");
            int __font = TITLE_FONT;
            int __w = Screen::tft.textWidth(__t.c_str(), __font);
            int __x = (screenW() - __w) / 2;
            __x = std::max(__x, LEFT_MARGIN);
            __x = std::min(__x, screenW() - __w - RIGHT_MARGIN);
            int __y = TOP_MARGIN;
            Screen::tft.drawString(__t, __x, __y, __font);
        }
        drawMessage("Please wait...", TOP_MARGIN + Screen::tft.fontHeight(TITLE_FONT) + 8, TEXT, BG, HEADING_FONT);

        bool res = installApp(appId);

        if (res)
            drawSuccess("Installed successfully");
        else
            drawError("Install failed");
    }

} // namespace AppManager

inline void appManager()
{
    AppManager::showInstaller();
}