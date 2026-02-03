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
    static const int TITLE_FONT = 2;
    static const int HEADING_FONT = 2;
    static const int BODY_FONT = 1;
    static const int BUTTON_FONT = 1;
    static const int DEFAULT_FONT = BODY_FONT;

    static const int LEFT_MARGIN = 8;
    static const int RIGHT_MARGIN = 8;
    static const int TOP_MARGIN = 8;
    static const int BOTTOM_MARGIN = 8;

    // ---------- helpers ----------

    String trimLines(const String &s)
    {
        int start = 0;
        int end = s.length() - 1;

        while (start <= end &&
               (s[start] == '\r' || s[start] == '\n' || isspace((unsigned char)s[start])))
            start++;

        while (end >= start &&
               (s[end] == '\r' || s[end] == '\n' || isspace((unsigned char)s[end])))
            end--;

        if (start > end)
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

    static void drawClippedString(int x, int y, int maxW, const String &s, int font = DEFAULT_FONT)
    {
        if (s.length() == 0)
            return;
        x = std::max(x, LEFT_MARGIN);
        tft.setTextDatum(TL_DATUM);
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

    static int screenW() { return Screen::tft.width(); }
    static int screenH() { return Screen::tft.height(); }

    static void clearScreen(uint16_t color = BG) { Screen::tft.fillScreen(color); }

    static void drawTitle(const String &title)
    {
        Screen::tft.setTextColor(TEXT, BG);
        int font = TITLE_FONT;
        int w = Screen::tft.textWidth(title.c_str(), font);
        int x = (screenW() - w) / 2;
        x = std::max(x, LEFT_MARGIN);
        x = std::min(x, screenW() - w - RIGHT_MARGIN);
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

    static void drawButton(const BtnRect &r, const char *label, uint16_t bg = PRIMARY, uint16_t fg = AT, int font = BUTTON_FONT)
    {
        int radius = std::min(8, r.h / 4);
        Screen::tft.fillRoundRect(r.x, r.y, r.w, r.h, radius, bg);
        Screen::tft.drawRoundRect(r.x, r.y, r.w, r.h, radius, ACCENT2);
        Screen::tft.setTextColor(fg, bg);

        int textW = Screen::tft.textWidth(label, font);
        int textH = Screen::tft.fontHeight(font);

        int tx = r.x + (r.w - textW) / 2;
        int ty = r.y + (r.h - textH) / 2;

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
            Screen::tft.fillRoundRect(x + 2, y + 2, fillWidth, height - 4, 2, color);

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

    // Core: download into Buffer with size limit and reserve to avoid fragmentation.
    static bool performGetBuffer(const String &url, Buffer &outBuf, size_t maxSize = 32 * 1024)
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
        // Reuse global secureClient to reduce heap churn
        secureClient.setInsecure();

        if (!http.begin(secureClient, url))
        {
            Serial.println("[ERROR] http.begin failed");
            http.end(); // safe
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

        int contentLen = http.getSize();
        if (contentLen > 0)
        {
            if ((size_t)contentLen > maxSize)
            {
                Serial.println("[ERROR] Content-Length exceeds max allowed");
                http.end();
                return false;
            }
            outBuf.data.reserve(contentLen);
        }
        else
        {
            outBuf.data.reserve(std::min((size_t)1024, maxSize));
        }

        WiFiClient &stream = http.getStream();
        const size_t BUF_SZ = 256;
        uint8_t tmp[BUF_SZ];
        unsigned long lastData = millis();

        while (http.connected())
        {
            int avail = stream.available();
            if (avail > 0)
            {
                int toRead = std::min(avail, (int)BUF_SZ);
                int r = stream.read(tmp, toRead);
                if (r > 0)
                {
                    if (outBuf.data.size() + (size_t)r > maxSize)
                    {
                        Serial.println("[ERROR] Download exceeds maximum allowed size");
                        http.end();
                        return false;
                    }
                    outBuf.data.insert(outBuf.data.end(), tmp, tmp + r);
                    lastData = millis();
                }
                else if (r < 0)
                {
                    Serial.println("[ERROR] Stream read error");
                    http.end();
                    return false;
                }
            }
            else
            {
                if (millis() - lastData > 5000)
                    break;
                delay(1);
            }
        }

        outBuf.ok = true;
        http.end();
        return true;
    }

    static String performGetString(const String &url, const String &fallback = "?")
    {
        if (WiFi.status() != WL_CONNECTED)
            return fallback;

        HTTPClient http;
        // reuse secureClient if you have one; otherwise http.begin(url) is fine
        if (!http.begin(url))
            return fallback;

        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            http.end();
            return fallback;
        }

        // read full stream (skip CR)
        String raw;
        WiFiClient &stream = http.getStream();
        unsigned long lastData = millis();
        const size_t BUF_SZ = 128;
        uint8_t buf[BUF_SZ];
        while (http.connected() || stream.available())
        {
            while (stream.available())
            {
                int r = stream.read(buf, BUF_SZ);
                if (r > 0)
                {
                    for (int i = 0; i < r; ++i)
                    {
                        if (buf[i] != '\r')
                            raw += (char)buf[i];
                    }
                    lastData = millis();
                }
            }
            if (millis() - lastData > 5000)
                break;
            delay(1);
        }
        http.end();

        raw.trim();
        if (raw.length() == 0)
            return fallback;

        // try to decode chunked encoding if raw starts with a hex chunk-size line
        String decoded;
        int pos = 0;
        bool chunkedDetected = false;
        while (pos < raw.length())
        {
            int nl = raw.indexOf('\n', pos);
            if (nl == -1)
            { // no newline - can't be chunk header
                break;
            }
            String line = raw.substring(pos, nl);
            line.trim();
            // check if line is only hex digits (chunk size)
            bool isHex = line.length() > 0;
            for (size_t i = 0; i < (size_t)line.length() && isHex; ++i)
            {
                char c = line[i];
                if (!isxdigit((unsigned char)c))
                    isHex = false;
            }
            if (!isHex)
                break;

            // parse chunk size
            unsigned long chunkSize = strtoul(line.c_str(), NULL, 16);
            chunkedDetected = true;
            pos = nl + 1; // move after chunk-size line
            if (chunkSize == 0)
            {
                // end of chunks
                break;
            }
            // append available bytes up to chunkSize (clamp if stream truncated)
            int available = raw.length() - pos;
            int take = (int)std::min<unsigned long>(chunkSize, (unsigned long)std::max(0, available));
            if (take > 0)
            {
                decoded += raw.substring(pos, pos + take);
                pos += take;
            }
            // skip a single LF that follows chunk payload if present
            if (pos < raw.length() && raw[pos] == '\n')
                pos++;
        }

        String finalBody = chunkedDetected ? decoded : raw;
        finalBody.trim();
        if (finalBody.length() == 0)
            return fallback;

        // return first non-empty line (most config files put name on first line)
        int firstNL = finalBody.indexOf('\n');
        if (firstNL == -1)
            return finalBody;
        // find first non-empty trimmed line
        int start = 0;
        while (start <= finalBody.length())
        {
            int nl2 = finalBody.indexOf('\n', start);
            String line = (nl2 == -1) ? finalBody.substring(start) : finalBody.substring(start, nl2);
            line.trim();
            if (line.length() > 0)
                return line;
            if (nl2 == -1)
                break;
            start = nl2 + 1;
        }

        return fallback;
    }

    // Convenience: image fetcher that enforces an expected fixed size (useful for icons)
    static bool performGetImageBuffer(const String &url, Buffer &outBuf, size_t expectedSize)
    {
        if (expectedSize == 0)
            return false;
        // allow small slack but require at least expectedSize
        const size_t maxSize = expectedSize + 1024;
        if (!performGetBuffer(url, outBuf, maxSize))
            return false;
        if (!outBuf.ok)
            return false;
        if (outBuf.data.size() < expectedSize)
        {
            Serial.printf("[ERROR] Image too small: got %u expected %u\n", (unsigned)outBuf.data.size(), (unsigned)expectedSize);
            return false;
        }
        // If file is larger than expected, keep it (some servers may append), but caller should handle.
        return true;
    }

    static bool performGetWithFallback(const String &url, Buffer &buf)
    {
        return performGetBuffer(url, buf);
    }

    static bool fetchAndWrite(const String &url, const String &path, const String &folderName, bool required, int &progress, int totalFiles, int currentFile)
    {
        Serial.println("[Download] " + url + " -> " + path);

        progress = (currentFile * 100) / totalFiles;
        clearScreen();
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

        // free the buffer memory immediately to return heap
        std::vector<uint8_t>().swap(dataBuf.data);

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
        const int SRC_W = 20;
        const int SRC_H = 20;
        const size_t SRC_PIX = SRC_W * SRC_H;
        if (!buf.ok || buf.data.size() < 4)
            return;
        const uint8_t *p = buf.data.data() + 4;
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
        Screen::tft.setTextColor(TEXT);
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

        int iconScale = 2;
        int iconW = 20 * iconScale;
        int iconH = 20 * iconScale;
        int iconX = screenW() - RIGHT_MARGIN - iconW;
        int iconY = TOP_MARGIN + Screen::tft.fontHeight(TITLE_FONT) + 12;

        int textX = LEFT_MARGIN;
        int textW = iconX - textX - 8;
        int nameY = iconY;
        drawClippedString(textX, nameY, textW, "Name: " + trimLines(appName), HEADING_FONT);
        drawClippedString(textX, nameY + Screen::tft.fontHeight(HEADING_FONT) + 4, textW, "Version: " + trimLines(version), BODY_FONT);
        drawClippedString(textX, nameY + Screen::tft.fontHeight(HEADING_FONT) + Screen::tft.fontHeight(BODY_FONT) + 8, textW, "Install this app to /programs/" + sanitizeFolderName(appName) + "?", BODY_FONT);

        int btnW = (screenW() - 32) / 2;
        int btnY = screenH() - BOTTOM_MARGIN - 48;
        BtnRect yes{LEFT_MARGIN, btnY, btnW, 40};
        BtnRect no{screenW() - RIGHT_MARGIN - btnW, btnY, btnW, 40};

        safePush20x20Icon(iconX, iconY, iconBuf, iconScale);
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

        Buffer iconBuf;
        String name;
        String version;

        // icon expected size: 4 bytes header + 20*20*2 = 804
        const size_t ICON_EXPECTED = 4 + 20 * 20 * 2;

        performGetImageBuffer(base + "icon-20x20.raw", iconBuf, ICON_EXPECTED);
        name = performGetString(base + "name.txt", "Unknown");
        version = performGetString(base + "version.txt", "?");

        if (name.length() == 0)
            name = "Unknown";
        if (version.length() == 0)
            version = "?";

        Serial.println("[Install] App name: " + name);
        Serial.println("[Install] Version: " + version);

        if (!confirmInstallPrompt(name, iconBuf, version))
            return false;

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
                    // free pkg buffer to return heap
                    std::vector<uint8_t>().swap(pkg.data);
                }
                progress = (currentFile * 100) / totalFiles;
                clearScreen();
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

        clearScreen();
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
