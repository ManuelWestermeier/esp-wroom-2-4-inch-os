#pragma once

#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <FS.h>
#include <vector>

#include "../io/read-string.hpp"
#include "../screen/index.hpp"
#include "../fs/enc-fs.hpp"

namespace AppManager
{

    struct Buffer
    {
        std::vector<uint8_t> data;
        bool ok = false;
    };

    // trim helper
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

    // perform GET (https first, fallback to http if fallback==true)
    bool performGet(const String &url, Buffer &buf, bool useHttps)
    {
        HTTPClient http;
        bool success = false;

        if (useHttps)
        {
            WiFiClientSecure client;
            client.setInsecure();
            if (!http.begin(client, url))
            {
                Serial.printf("http begin failed (https) for %s\n", url.c_str());
            }
            else
            {
                int code = http.GET();
                Serial.printf("http GET %s -> %d\n", url.c_str(), code);
                if (code == HTTP_CODE_OK)
                {
                    WiFiClient *stream = &http.getStream();
                    size_t len = http.getSize();
                    if (len > 0)
                    {
                        buf.data.resize(len);
                        stream->readBytes(buf.data.data(), len);
                        buf.ok = true;
                        success = true;
                    }
                }
                http.end();
            }
        }

        if (!success && !useHttps)
        {
            WiFiClient client;
            if (!http.begin(client, url))
            {
                Serial.printf("http begin failed (http) for %s\n", url.c_str());
            }
            else
            {
                int code = http.GET();
                Serial.printf("http GET %s -> %d\n", url.c_str(), code);
                if (code == HTTP_CODE_OK)
                {
                    WiFiClient *stream = &http.getStream();
                    size_t len = http.getSize();
                    if (len > 0)
                    {
                        buf.data.resize(len);
                        stream->readBytes(buf.data.data(), len);
                        buf.ok = true;
                        success = true;
                    }
                }
                http.end();
            }
        }
        return success;
    }

    // wrapper that tries https then http fallback automatically
    bool performGetWithFallback(const String &url, Buffer &buf)
    {
        bool ok = performGet(url, buf, true);
        if (!ok)
        {
            Serial.printf("HTTPS failed for %s, trying HTTP...\n", url.c_str());
            int p = url.indexOf("//");
            String httpUrl = url;
            if (p >= 0)
            {
                httpUrl = String("http://") + url.substring(p + 2);
            }
            ok = performGet(httpUrl, buf, false);
        }
        return ok && buf.ok;
    }

    bool fetchAndWrite(const String &url, const String &path, const String &appId, bool required)
    {
        Buffer data;
        bool ok = performGetWithFallback(url, data);
        if (!ok)
        {
            Serial.printf("Failed to download %s\n", url.c_str());
            if (required)
                return false;
            return true; // optional file failed but not fatal
        }

        // ensure directory exists
        if (!ENC_FS::exists({"programs"}))
            ENC_FS::mkDir({"programs"});
        if (!ENC_FS::exists({"programs", appId}))
            ENC_FS::mkDir({"programs", appId});

        bool written = ENC_FS::writeFile({"programs", appId, path}, 0, 0, data.data);
        if (!written)
        {
            Serial.printf("Failed to open %s for write\n", path.c_str());
            return false;
        }
        return true;
    }

    std::vector<String> parsePkgTxt(const Buffer &buf)
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

    // simple rectangle helper
    struct Rect
    {
        int x, y, w, h;
        bool contains(int px, int py) const
        {
            return px >= x && px < (x + w) && py >= y && py < (y + h);
        }
    };

    static void drawButton(const Rect &r, const char *label, uint16_t bg = TFT_BLUE, uint16_t fg = TFT_WHITE)
    {
        Screen::tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, bg);
        Screen::tft.setTextColor(fg, bg);
        Screen::tft.drawString(label, r.x + 10, r.y + (r.h - 16) / 2, 2);
    }

    // Wait for choice between two buttons using Screen::getTouchPos() with serial fallback.
    // returns 'i' for first button, 'c' for second button
    static char waitForTwoButtonChoice(const Rect &a, const Rect &b)
    {
        while (true)
        {
            // use Screen touch API
            Screen::TouchPos tp = Screen::getTouchPos();
            if (tp.clicked)
            {
                int tx = (int)tp.x;
                int ty = (int)tp.y;
                if (a.contains(tx, ty))
                    return 'i';
                if (b.contains(tx, ty))
                    return 'c';
                // minor debounce
                delay(50);
            }

            // serial fallback
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
        }
    }

    bool installApp(const String &appId)
    {
        String base = "https://" + appId + ".onrender.com/";

        // create directories up-front so progress saves can be written
        if (!ENC_FS::exists({"programs"}))
            ENC_FS::mkDir({"programs"});
        if (!ENC_FS::exists({"programs", appId}))
            ENC_FS::mkDir({"programs", appId});

        // core files
        std::vector<std::pair<String, String>> toFetch;
        toFetch.push_back({base + "entry.lua", "entry.lua"});
        toFetch.push_back({base + "icon-20x20.raw", "icon-20x20.raw"});
        toFetch.push_back({base + "name.txt", "name.txt"});
        toFetch.push_back({base + "version.txt", "version.txt"});

        for (size_t i = 0; i < toFetch.size(); ++i)
        {
            String url = toFetch[i].first;
            String path = toFetch[i].second;

            Screen::tft.fillRect(0, 100, 320, 60, TFT_BLACK);
            Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
            Screen::tft.drawString("Downloading:", 10, 100, 2);
            Screen::tft.drawString(path, 10, 120, 2);

            bool ok = fetchAndWrite(url, path, appId, true);
            if (!ok)
            {
                Serial.printf("Required file failed: %s\n", path.c_str());
                return false;
            }

            int pct = (int)((i + 1) * 100 / toFetch.size());
            Screen::tft.fillRect(10, 160, 300, 12, TFT_DARKGREY);
            Screen::tft.fillRect(10, 160, (int)(3.0 * pct), 12, TFT_GREEN);
        }

        // pkg.txt
        Buffer pkg;
        bool fetchedPkg = performGetWithFallback(base + "pkg.txt", pkg);
        if (!fetchedPkg)
        {
            Screen::tft.fillRect(0, 100, 320, 40, TFT_BLACK);
            Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
            Screen::tft.drawString("No pkg.txt found, done.", 10, 100, 2);
            delay(800);
            return true;
        }

        auto files = parsePkgTxt(pkg);
        if (files.empty())
        {
            Screen::tft.fillRect(0, 100, 320, 40, TFT_BLACK);
            Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
            Screen::tft.drawString("pkg.txt empty, done.", 10, 100, 2);
            delay(600);
            return true;
        }

        for (size_t i = 0; i < files.size(); ++i)
        {
            String rp = files[i];
            Screen::tft.fillRect(0, 100, 320, 60, TFT_BLACK);
            Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
            Screen::tft.drawString("Extra file:", 10, 100, 2);
            Screen::tft.drawString(rp, 10, 120, 2);

            bool ok = fetchAndWrite(base + rp, rp, appId, false);

            int pct = (int)((i + 1) * 100 / files.size());
            Screen::tft.fillRect(10, 160, 300, 12, TFT_DARKGREY);
            Screen::tft.fillRect(10, 160, (int)(3.0 * pct), 12, TFT_GREEN);

            if (!ok)
            {
                Serial.printf("Optional file failed: %s\n", rp.c_str());
                Screen::tft.drawString("Optional file failed", 10, 180, 2);
                delay(400);
            }
            else
            {
                Screen::tft.drawString("Saved", 10, 180, 2);
                delay(200);
            }
        }

        return true;
    }

    void showInstaller()
    {
        // header
        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        Screen::tft.drawString("App Manager", 10, 10, 2);

        Rect installRect{20, 50, 160, 50};
        Rect cancelRect{200, 50, 100, 50};

        // buttons
        drawButton(installRect, "Install new app", TFT_GREEN, TFT_BLACK);
        drawButton(cancelRect, "Cancel", TFT_RED, TFT_BLACK);

        char choice = waitForTwoButtonChoice(installRect, cancelRect);
        if (choice != 'i')
            return;

        // get app id via serial (on-screen keyboard not provided)
        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        Screen::tft.drawString("Enter App ID on serial input", 10, 10, 2);
        String appId = readString("App ID: ");
        appId.trim();
        if (appId.length() == 0)
            return;

        // preview
        Buffer iconBuf, nameBuf, verBuf;
        performGetWithFallback("https://" + appId + ".onrender.com/icon-20x20.raw", iconBuf);
        performGetWithFallback("https://" + appId + ".onrender.com/name.txt", nameBuf);
        performGetWithFallback("https://" + appId + ".onrender.com/version.txt", verBuf);

        Screen::tft.fillScreen(TFT_BLACK);

        if (iconBuf.ok && iconBuf.data.size() >= 4 + 20 * 20 * 2)
        {
            uint16_t *pix = (uint16_t *)(iconBuf.data.data() + 4);
            Screen::tft.pushImage(10, 10, 20, 20, pix);
        }
        if (nameBuf.ok)
        {
            String n((const char *)nameBuf.data.data(), nameBuf.data.size());
            Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
            Screen::tft.drawString("Name: " + trimLines(n), 40, 10, 2);
        }
        if (verBuf.ok)
        {
            String v((const char *)verBuf.data.data(), verBuf.data.size());
            Screen::tft.drawString("Version: " + trimLines(v), 40, 30, 2);
        }

        Rect okRect{20, 60, 100, 50};
        Rect cancel2Rect{140, 60, 100, 50};
        drawButton(okRect, "Install", TFT_GREEN, TFT_BLACK);
        drawButton(cancel2Rect, "Cancel", TFT_RED, TFT_BLACK);

        char confirm = waitForTwoButtonChoice(okRect, cancel2Rect);
        if (confirm != 'i')
            return;

        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        Screen::tft.drawString("Installing...", 10, 10, 2);

        bool result = installApp(appId);

        Screen::tft.fillScreen(result ? TFT_GREEN : TFT_RED);
        Screen::tft.setTextColor(TFT_BLACK, result ? TFT_GREEN : TFT_RED);
        Screen::tft.drawString(result ? "Installed" : "Install failed", 10, 10, 2);
        delay(1500);
    }
}

void appManager()
{
    AppManager::showInstaller();
}
