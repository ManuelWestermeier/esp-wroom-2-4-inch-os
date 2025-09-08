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

    // sanitize a string to use as a safe folder name: replace non-alnum with '_'
    String sanitizeFolderName(const String &s)
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
            {
                out += c;
            }
            else
            {
                out += '_';
            }
        }
        if (out.length() == 0)
            out = "app";
        return out;
    }

    // perform GET: robust reading of stream (works if getSize()==0 or unknown)
    bool performGet(const String &url, Buffer &buf, bool useHttps)
    {
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
                        size_t read = 0;
                        while (read < len)
                        {
                            size_t r = stream->readBytes(buf.data.data() + read, len - read);
                            if (r == 0)
                                break;
                            read += r;
                        }
                    }
                    else
                    {
                        // read until stream closed / no data
                        buf.data.clear();
                        unsigned long start = millis();
                        while (stream->connected() || stream->available())
                        {
                            while (stream->available())
                            {
                                int c = stream->read();
                                if (c < 0)
                                    break;
                                buf.data.push_back((uint8_t)c);
                            }
                            // safety timeout in case stream hangs
                            if (millis() - start > 15000)
                                break;
                            delay(1);
                        }
                    }
                    buf.ok = !buf.data.empty();
                    success = buf.ok;
                }
                else
                {
                    Serial.printf("HTTP code %d for %s\n", code, url.c_str());
                }
                http.end();
            }
        }
        else
        {
            WiFiClient client;
            if (!http.begin(client, url))
            {
                Serial.printf("http.begin (http) failed: %s\n", url.c_str());
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
                        size_t read = 0;
                        while (read < len)
                        {
                            size_t r = stream->readBytes(buf.data.data() + read, len - read);
                            if (r == 0)
                                break;
                            read += r;
                        }
                    }
                    else
                    {
                        buf.data.clear();
                        unsigned long start = millis();
                        while (stream->connected() || stream->available())
                        {
                            while (stream->available())
                            {
                                int c = stream->read();
                                if (c < 0)
                                    break;
                                buf.data.push_back((uint8_t)c);
                            }
                            if (millis() - start > 15000)
                                break;
                            delay(1);
                        }
                    }
                    buf.ok = !buf.data.empty();
                    success = buf.ok;
                }
                else
                {
                    Serial.printf("HTTP code %d for %s\n", code, url.c_str());
                }
                http.end();
            }
        }
        return success;
    }

    // try https then http
    bool performGetWithFallback(const String &url, Buffer &buf)
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

    bool fetchAndWrite(const String &url, const String &path, const String &folderName, bool required)
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

        bool written = ENC_FS::writeFile({"programs", folderName, path}, 0, 0, data.data);
        if (!written)
        {
            Serial.printf("ENC_FS::writeFile failed for %s\n", path.c_str());
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

    // Wait for user to tap one of two buttons using Screen::getTouchPos(); serial fallback returns 'i' or 'c'.
    static char waitForTwoButtonChoice(const Rect &a, const Rect &b)
    {
        unsigned long start = millis();
        while (true)
        {
            Screen::TouchPos tp = Screen::getTouchPos();
            if (tp.clicked)
            {
                int tx = (int)tp.x;
                int ty = (int)tp.y;
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

            // small sleep so we don't starve CPU
            delay(10);

            // optional: break out after a long timeout (not used by us, but helpful during debugging)
            if (millis() - start > 120000) // 2 minutes timeout
            {
                Serial.println("waitForTwoButtonChoice: timeout");
                return 'c';
            }
        }
    }

    // Show a confirmation dialog with app name and icon; returns true if user confirms
    static bool confirmInstallPrompt(const String &appName, const Buffer &iconBuf, const String &version)
    {
        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        Screen::tft.drawString("Install App?", 10, 6, 2);

        // parse icon header: first 4 bytes expected little-endian width(2) height(2)
        bool showedImage = false;
        if (iconBuf.ok && iconBuf.data.size() >= 4)
        {
            uint16_t w = (uint16_t)iconBuf.data[0] | ((uint16_t)iconBuf.data[1] << 8);
            uint16_t h = (uint16_t)iconBuf.data[2] | ((uint16_t)iconBuf.data[3] << 8);
            size_t expected = 4 + (size_t)w * (size_t)h * 2;
            Serial.printf("icon header w=%u h=%u expected bytes=%u actual=%u\n", (unsigned)w, (unsigned)h, (unsigned)expected, (unsigned)iconBuf.data.size());
            if (w > 0 && h > 0 && iconBuf.data.size() >= expected)
            {
                uint16_t *pix = (uint16_t *)(iconBuf.data.data() + 4);
                // safe to call pushImage now
                Screen::tft.pushImage(10, 36, w, h, pix);
                showedImage = true;
            }
            else
            {
                Serial.println("Icon header invalid or data too small; skipping image.");
            }
        }

        if (!showedImage)
        {
            Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
            Screen::tft.drawString("[no icon]", 10, 36, 2);
        }

        if (appName.length() > 0)
            Screen::tft.drawString("Name: " + trimLines(appName), 40, 36, 2);
        if (version.length() > 0)
            Screen::tft.drawString("Version: " + trimLines(version), 40, 56, 2);

        Rect yes{40, 100, 100, 50};
        Rect no{180, 100, 80, 50};
        drawButton(yes, "Yes", TFT_GREEN, TFT_BLACK);
        drawButton(no, "No", TFT_RED, TFT_BLACK);

        char c = waitForTwoButtonChoice(yes, no);
        return (c == 'i');
    }

    // top-level install; requires files to be downloaded after user confirmation
    bool installApp(const String &rawAppId)
    {
        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("installApp: WiFi not connected - aborting");
            Screen::tft.fillScreen(TFT_RED);
            Screen::tft.setTextColor(TFT_WHITE, TFT_RED);
            Screen::tft.drawString("WiFi not connected", 10, 10, 2);
            delay(800);
            return false;
        }

        // build base URL and sanitized folder name
        String base;
        if (rawAppId.indexOf("http://") == 0 || rawAppId.indexOf("https://") == 0)
        {
            base = rawAppId;
        }
        else if (rawAppId.indexOf('/') != -1 && rawAppId.indexOf('.') != -1)
        {
            // treat as host/path
            base = rawAppId;
        }
        else
        {
            // default domain if user supplied a simple id
            base = "https://" + rawAppId + ".onrender.com/";
        }
        if (!base.endsWith("/"))
            base += "/";

        // folder name for disk (sanitized)
        String folderName = sanitizeFolderName(rawAppId);

        // metadata fetch (name, version, icon) WITHOUT creating folders yet
        Buffer nameBuf, verBuf, iconBuf;
        performGetWithFallback(base + "name.txt", nameBuf);
        performGetWithFallback(base + "version.txt", verBuf);
        performGetWithFallback(base + "icon-20x20.raw", iconBuf);

        String name = "";
        if (nameBuf.ok)
            name = String((const char *)nameBuf.data.data(), nameBuf.data.size());

        String version = "";
        if (verBuf.ok)
            version = String((const char *)verBuf.data.data(), verBuf.data.size());

        // ask user to confirm using the metadata
        bool confirmed = confirmInstallPrompt(name, iconBuf, version);
        if (!confirmed)
        {
            Serial.println("User cancelled install (confirmation step)");
            return false;
        }

        // create directories
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

        // download core required files
        for (size_t i = 0; i < core.size(); ++i)
        {
            Screen::tft.fillRect(0, 150, 320, 80, TFT_BLACK);
            Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
            Screen::tft.drawString("Downloading:", 10, 150, 2);
            Screen::tft.drawString(core[i].second, 10, 170, 2);

            bool ok = fetchAndWrite(core[i].first, core[i].second, folderName, true);
            if (!ok)
            {
                Serial.printf("Failed required: %s\n", core[i].second.c_str());
                Screen::tft.fillScreen(TFT_RED);
                Screen::tft.setTextColor(TFT_BLACK, TFT_RED);
                Screen::tft.drawString("Install failed", 10, 10, 2);
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

        auto extraFiles = parsePkgTxt(pkg);
        for (size_t i = 0; i < extraFiles.size(); ++i)
        {
            Screen::tft.fillRect(0, 150, 320, 80, TFT_BLACK);
            Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
            Screen::tft.drawString("Extra:", 10, 150, 2);
            Screen::tft.drawString(extraFiles[i], 10, 170, 2);

            bool ok = fetchAndWrite(base + extraFiles[i], extraFiles[i], folderName, false);

            int pct = (int)((i + 1) * 100 / extraFiles.size());
            Screen::tft.fillRect(10, 210, 300, 10, TFT_DARKGREY);
            Screen::tft.fillRect(10, 210, (int)(3.0 * pct), 10, TFT_GREEN);

            if (!ok)
            {
                Serial.printf("Optional download failed: %s\n", extraFiles[i].c_str());
                Screen::tft.drawString("Optional failed", 10, 185, 2);
                delay(300);
            }
            else
            {
                Screen::tft.drawString("Saved", 10, 185, 2);
                delay(150);
            }
        }

        return true;
    }

    void showInstaller()
    {
        // main screen
        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        Screen::tft.drawString("App Manager", 10, 10, 2);

        Rect installRect{20, 50, 160, 50};
        Rect cancelRect{200, 50, 100, 50};

        drawButton(installRect, "Install new app", TFT_GREEN, TFT_BLACK);
        drawButton(cancelRect, "Cancel", TFT_RED, TFT_BLACK);

        char choice = waitForTwoButtonChoice(installRect, cancelRect);
        if (choice != 'i')
            return;

        // ask for app id - serial entry (no on-screen keyboard)
        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        Screen::tft.drawString("Enter App ID on serial", 10, 10, 2);
        String appId = readString("App ID: ");
        appId.trim();
        if (appId.length() == 0)
            return;

        // attempt install
        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        Screen::tft.drawString("Preparing...", 10, 10, 2);

        bool res = installApp(appId);

        Screen::tft.fillScreen(res ? TFT_GREEN : TFT_RED);
        Screen::tft.setTextColor(TFT_BLACK, res ? TFT_GREEN : TFT_RED);
        Screen::tft.drawString(res ? "Installed" : "Install failed", 10, 10, 2);
        delay(1200);
    }
}

void appManager()
{
    AppManager::showInstaller();
}
