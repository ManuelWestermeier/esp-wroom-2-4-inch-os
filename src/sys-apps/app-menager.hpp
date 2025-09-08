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

namespace AppManager
{

    // simple byte-buffer wrapper used throughout this file
    struct Buffer
    {
        std::vector<uint8_t> data;
        bool ok = false;
    };

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

    // sanitize a string to use as a safe folder name: replace non-alnum with '_'
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

    // draw a string clipped to max width using TFT_eSPI::textWidth
    static void drawClippedString(int x, int y, int maxW, int font, const String &s)
    {
        if (s.length() == 0)
            return;
        const char *cs = s.c_str();
        int w = Screen::tft.textWidth(cs, font);
        if (w <= maxW)
        {
            Screen::tft.drawString(s, x, y, font);
            return;
        }
        // shorten until fits, append ellipsis
        String t = s;
        while (t.length() > 0)
        {
            t = t.substring(0, t.length() - 1);
            String tp = t + "...";
            if (Screen::tft.textWidth(tp.c_str(), font) <= maxW)
            {
                Screen::tft.drawString(tp, x, y, font);
                return;
            }
        }
        // nothing fits; draw nothing
    }

    // ---------- networking (robust read) ----------

    // perform GET: robust reading of stream (works if getSize()==0 or unknown).
    // Limits response size to avoid OOM.
    static bool performGet(const String &url, Buffer &outBuf, bool useHttps)
    {
        const size_t MAX_BYTES = 200 * 1024; // 200 KB cap
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("performGet: WiFi not connected");
            return false;
        }

        HTTPClient http;
        bool success = false;

        if (useHttps)
        {
            WiFiClientSecure client;
            client.setInsecure();
            if (!http.begin(client, url))
            {
                Serial.printf("http.begin (https) failed: %s\n", url.c_str());
                return false;
            }
        }
        else
        {
            WiFiClient client;
            if (!http.begin(client, url))
            {
                Serial.printf("http.begin (http) failed: %s\n", url.c_str());
                return false;
            }
        }

        int code = http.GET();
        Serial.printf("http GET %s -> %d\n", url.c_str(), code);
        if (code != HTTP_CODE_OK)
        {
            http.end();
            Serial.printf("HTTP code %d for %s\n", code, url.c_str());
            return false;
        }

        WiFiClient *stream = &http.getStream();
        int len = http.getSize(); // may be -1 if unknown
        if (len > 0)
        {
            if ((size_t)len > MAX_BYTES)
            {
                Serial.printf("performGet: Content-Length %d exceeds cap %u\n", len, (unsigned)MAX_BYTES);
                http.end();
                return false;
            }
            outBuf.data.resize((size_t)len);
            size_t read = 0;
            unsigned long start = millis();
            while (read < (size_t)len)
            {
                size_t r = stream->readBytes(outBuf.data.data() + read, (size_t)len - read);
                if (r == 0)
                {
                    if (millis() - start > 15000)
                        break;
                    delay(1);
                    continue;
                }
                read += r;
            }
        }
        else
        {
            // unknown size — read until closed or cap reached
            outBuf.data.clear();
            unsigned long start = millis();
            while ((stream->connected() || stream->available()) && outBuf.data.size() < MAX_BYTES)
            {
                while (stream->available())
                {
                    int c = stream->read();
                    if (c < 0)
                        break;
                    outBuf.data.push_back((uint8_t)c);
                }
                if (millis() - start > 15000)
                    break;
                delay(1);
            }
        }

        outBuf.ok = !outBuf.data.empty();
        success = outBuf.ok;
        http.end();
        return success;
    }

    // try https then http
    static bool performGetWithFallback(const String &url, Buffer &buf)
    {
        if (performGet(url, buf, true))
            return true;
        Serial.printf("HTTPS failed for %s, trying HTTP\n", url.c_str());
        int p = url.indexOf("//");
        if (p >= 0)
        {
            String httpUrl = String("http://") + url.substring(p + 2);
            return performGet(httpUrl, buf, false);
        }
        return false;
    }

    // ---------- filesystem + fetch ----------

    static bool fetchAndWrite(const String &url, const String &path, const String &folderName, bool required)
    {
        Buffer dataBuf;
        bool ok = performGetWithFallback(url, dataBuf);
        if (!ok)
        {
            Serial.printf("Failed to download %s\n", url.c_str());
            if (required)
                return false;
            return true;
        }

        // ensure directory exists (ENC_FS)
        if (!ENC_FS::exists({"programs"}))
        {
            if (!ENC_FS::mkDir({"programs"}))
            {
                Serial.println("ENC_FS::mkDir(/programs) failed");
                if (required)
                    return false;
            }
        }
        if (!ENC_FS::exists({"programs", folderName}))
        {
            if (!ENC_FS::mkDir({"programs", folderName}))
            {
                Serial.printf("ENC_FS::mkDir(/programs/%s) failed\n", folderName.c_str());
                if (required)
                    return false;
            }
        }

        bool written = ENC_FS::writeFile({"programs", folderName, path}, 0, 0, dataBuf.data);
        if (!written)
        {
            Serial.printf("ENC_FS::writeFile failed for %s\n", path.c_str());
            return false;
        }
        return true;
    }

    // parse pkg.txt into vector of relative paths
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

    // ---------- icon handling ----------
    // all icons are 20x20 RGB565 with 4-byte header (ignored).
    // Build a 20x20 temporary buffer, pad/truncate to 800 bytes and push.
    static void safePush20x20Icon(int x, int y, const Buffer &buf)
    {
        const size_t ICON_PIX = 20 * 20;
        const size_t ICON_BYTES = ICON_PIX * 2; // 800
        if (!buf.ok || buf.data.size() < 4)
            return;
        const uint8_t *p = buf.data.data() + 4;
        size_t payload = buf.data.size() - 4;

        // temporary buffer (stack) is OK for 800 uint16_t
        uint16_t tmp[ICON_PIX];
        for (size_t i = 0; i < ICON_PIX; ++i)
        {
            size_t off = i * 2;
            if (off + 1 < payload)
                tmp[i] = (uint16_t)p[off] | ((uint16_t)p[off + 1] << 8);
            else
                tmp[i] = 0; // pad black
        }

        Screen::tft.pushImage(x, y, 20, 20, tmp);
    }

    // ---------- UI helpers (no Rect redefinition) ----------
    // Use a unique button rectangle type to avoid colliding with project Rect
    struct BtnRect
    {
        int x, y, w, h;
        bool contains(int px, int py) const { return px >= x && px < (x + w) && py >= y && py < (y + h); }
    };

    static void drawButton(const BtnRect &r, const char *label, uint16_t bg = TFT_BLUE, uint16_t fg = TFT_WHITE)
    {
        Screen::tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, bg);
        Screen::tft.setTextColor(fg, bg);
        drawClippedString(r.x + 6, r.y + (r.h - 16) / 2, r.w - 12, 2, String(label));
    }

    // wait for two-button choice using Screen::getTouchPos() + serial fallback
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
                delay(50);
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
            delay(10);
            if (millis() - start > 120000)
            {
                Serial.println("waitForTwoButtonChoice: timeout -> cancel");
                return 'c';
            }
        }
    }

    // confirmation dialog: show icon (20x20), name and version
    static bool confirmInstallPrompt(const String &appName, const Buffer &iconBuf, const String &version)
    {
        const int LEFT = 8;
        const int TOP = 8;
        const int AVAIL_W = 320 - LEFT * 2;

        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        drawClippedString(LEFT, TOP, AVAIL_W, 4, String("Install App?"));

        int iconY = TOP + 28;
        safePush20x20Icon(LEFT, iconY, iconBuf);

        int textX = LEFT + 28;
        drawClippedString(textX, iconY, AVAIL_W - 28, 2, String("Name: ") + trimLines(appName));
        drawClippedString(textX, iconY + 20, AVAIL_W - 28, 2, String("Version: ") + trimLines(version));

        BtnRect yes{LEFT + 20, 160, 110, 50};
        BtnRect no{LEFT + 160, 160, 110, 50};
        drawButton(yes, "Install", TFT_GREEN, TFT_BLACK);
        drawButton(no, "Cancel", TFT_RED, TFT_BLACK);

        char c = waitForTwoButtonChoice(yes, no);
        return (c == 'i');
    }

    // ---------- high level install flow ----------

    static bool ensureWiFiConnected(unsigned long timeoutMs = 8000)
    {
        if (WiFi.status() == WL_CONNECTED)
            return true;
        unsigned long start = millis();
        Serial.println("Waiting for WiFi...");
        while (millis() - start < timeoutMs)
        {
            if (WiFi.status() == WL_CONNECTED)
                return true;
            delay(100);
        }
        Serial.println("WiFi not connected (timeout)");
        return false;
    }

    static bool installApp(const String &rawAppId)
    {
        if (!ensureWiFiConnected(10000))
        {
            Serial.println("installApp: WiFi not connected - aborting");
            Screen::tft.fillScreen(TFT_RED);
            Screen::tft.setTextColor(TFT_WHITE, TFT_RED);
            drawClippedString(8, 10, 320 - 16, 2, String("WiFi not connected"));
            delay(800);
            return false;
        }

        // build base URL
        String base = rawAppId;
        if (rawAppId.indexOf("http://") == -1 && rawAppId.indexOf("https://") == -1 && rawAppId.indexOf('.') == -1)
            base = "https://" + rawAppId + ".onrender.com/";
        if (!base.endsWith("/"))
            base += "/";

        String folderName = sanitizeFolderName(rawAppId);

        // fetch metadata first (no writes)
        Buffer nameBuf, verBuf, iconBuf;
        performGetWithFallback(base + "name.txt", nameBuf);
        performGetWithFallback(base + "version.txt", verBuf);
        performGetWithFallback(base + "icon-20x20.raw", iconBuf);

        String name = "";
        if (nameBuf.ok && nameBuf.data.size() < 4096)
            name = String((const char *)nameBuf.data.data(), nameBuf.data.size());
        String version = "";
        if (verBuf.ok && verBuf.data.size() < 4096)
            version = String((const char *)verBuf.data.data(), verBuf.data.size());

        bool confirmed = confirmInstallPrompt(name, iconBuf, version);
        if (!confirmed)
        {
            Serial.println("User cancelled install (confirmation step)");
            return false;
        }

        // create dirs
        if (!ENC_FS::exists({"programs"}))
        {
            if (!ENC_FS::mkDir({"programs"}))
            {
                Serial.println("Failed to create /programs");
                return false;
            }
        }
        if (!ENC_FS::exists({"programs", folderName}))
        {
            if (!ENC_FS::mkDir({"programs", folderName}))
            {
                Serial.printf("Failed to create /programs/%s\n", folderName.c_str());
                return false;
            }
        }

        // core files to download
        std::vector<std::pair<String, String>> core = {
            {base + "entry.lua", "entry.lua"},
            {base + "icon-20x20.raw", "icon-20x20.raw"},
            {base + "name.txt", "name.txt"},
            {base + "version.txt", "version.txt"}};

        for (size_t i = 0; i < core.size(); ++i)
        {
            Screen::tft.fillRect(0, 150, 320, 80, TFT_BLACK);
            Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
            drawClippedString(8, 150, 320 - 16, 2, String("Downloading: ") + core[i].second);

            if (!ensureWiFiConnected(5000))
            {
                Serial.println("Lost WiFi during download");
                Screen::tft.drawString("WiFi lost", 8, 170, 2);
                return false;
            }

            if (!fetchAndWrite(core[i].first, core[i].second, folderName, true))
            {
                Serial.printf("Failed required: %s\n", core[i].second.c_str());
                Screen::tft.fillScreen(TFT_RED);
                Screen::tft.setTextColor(TFT_BLACK, TFT_RED);
                drawClippedString(8, 10, 320 - 16, 2, String("Install failed"));
                delay(1200);
                return false;
            }

            int pct = (int)((i + 1) * 100 / core.size());
            Screen::tft.fillRect(10, 210, 300, 10, TFT_DARKGREY);
            Screen::tft.fillRect(10, 210, (int)(3.0 * pct), 10, TFT_GREEN);
        }

        // optional pkg.txt
        Buffer pkg;
        if (!performGetWithFallback(base + "pkg.txt", pkg))
        {
            Serial.println("No pkg.txt; finishing install.");
            return true;
        }

        auto extras = parsePkgTxt(pkg);
        for (size_t i = 0; i < extras.size(); ++i)
        {
            drawClippedString(8, 150, 320 - 16, 2, String("Extra: ") + extras[i]);
            if (!performGetWithFallback(base + extras[i], pkg))
            {
                Serial.printf("Optional failed %s\n", extras[i].c_str());
                continue;
            }
            if (!ENC_FS::writeFile({"programs", folderName, extras[i]}, 0, 0, pkg.data))
            {
                Serial.printf("writeFile failed %s\n", extras[i].c_str());
            }
        }

        return true;
    }

    // ---------- UI entry point ----------

    static void showInstaller()
    {
        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        drawClippedString(8, 8, 320 - 16, 4, String("App Manager"));

        BtnRect installRect{16, 48, 140, 44};
        BtnRect cancelRect{172, 48, 140, 44};

        drawButton(installRect, "Install new app", TFT_GREEN, TFT_BLACK);
        drawButton(cancelRect, "Cancel", TFT_RED, TFT_BLACK);

        char choice = waitForTwoButtonChoice(installRect, cancelRect);
        if (choice != 'i')
            return;

        Screen::tft.fillScreen(TFT_BLACK);
        drawClippedString(8, 8, 320 - 16, 2, String("Enter App ID on serial"));
        String appId = readString("App ID: ");
        appId.trim();
        if (appId.length() == 0)
            return;

        Screen::tft.fillScreen(TFT_BLACK);
        drawClippedString(8, 8, 320 - 16, 2, String("Preparing..."));

        bool res = installApp(appId);

        Screen::tft.fillScreen(res ? TFT_GREEN : TFT_RED);
        Screen::tft.setTextColor(TFT_BLACK, res ? TFT_GREEN : TFT_RED);
        drawClippedString(8, 8, 320 - 16, 2, res ? String("Installed") : String("Install failed"));
        delay(1200);
    }

} // namespace AppManager

// small global entry point kept for compatibility with your previous code
inline void appManager()
{
    AppManager::showInstaller();
}
