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

    bool fetchAndWrite(const String &url, const String &path, const String &appId, bool required)
    {
        Buffer data;
        bool ok = performGet(url, data, true);
        if (!ok)
        {
            Serial.printf("HTTPS failed for %s, trying HTTP...\n", url.c_str());
            ok = performGet(String("http://") + url.substring(url.indexOf("//") + 2), data, false);
        }
        if (!ok || !data.ok)
        {
            Serial.printf("Failed to download %s\n", url.c_str());
            if (required)
                return false;
            return true;
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

    bool installApp(const String &appId)
    {
        String base = "https://" + appId + ".duckdns.org/";

        if (!fetchAndWrite(base + "entry.lua", "entry.lua", appId, true))
            return false;
        if (!fetchAndWrite(base + "icon-20x20.raw", "icon-20x20.raw", appId, true))
            return false;
        if (!fetchAndWrite(base + "name.txt", "name.txt", appId, true))
            return false;
        if (!fetchAndWrite(base + "version.txt", "version.txt", appId, true))
            return false;

        Buffer pkg;
        if (!performGet(base + "pkg.txt", pkg, true))
        {
            Serial.printf("Failed to fetch pkg.txt\n");
            return false;
        }
        auto files = parsePkgTxt(pkg);
        for (auto &rp : files)
        {
            if (!fetchAndWrite(base + rp, rp, appId, false))
            {
                Serial.printf("Failed extra: %s\n", rp.c_str());
            }
        }
        return true;
    }

    void showInstaller()
    {
        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        Screen::tft.drawString("App Manager", 10, 10, 2);

        // buttons
        Screen::tft.fillRoundRect(20, 50, 120, 40, 8, TFT_BLUE);
        Screen::tft.drawString("Install new app", 30, 60, 2);
        Screen::tft.fillRoundRect(160, 50, 80, 40, 8, TFT_RED);
        Screen::tft.drawString("Cancel", 170, 60, 2);

        // wait for input
        String cmd = readString("Enter 'i' to install or 'c' to cancel: ");
        if (cmd != "i")
            return;

        String appId = readString("Enter app id: ");

        // preview
        Buffer iconBuf;
        performGet("https://" + appId + ".duckdns.org/icon-20x20.raw", iconBuf, true);
        Buffer nameBuf;
        performGet("https://" + appId + ".duckdns.org/name.txt", nameBuf, true);
        Buffer verBuf;
        performGet("https://" + appId + ".duckdns.org/version.txt", verBuf, true);

        Screen::tft.fillScreen(TFT_BLACK);
        if (iconBuf.ok && iconBuf.data.size() >= 4 + 20 * 20 * 2)
        {
            uint16_t *pix = (uint16_t *)(iconBuf.data.data() + 4);
            Screen::tft.pushImage(10, 10, 20, 20, pix);
        }
        if (nameBuf.ok)
        {
            String n((const char *)nameBuf.data.data(), nameBuf.data.size());
            Screen::tft.drawString("Name: " + trimLines(n), 40, 10, 2);
        }
        if (verBuf.ok)
        {
            String v((const char *)verBuf.data.data(), verBuf.data.size());
            Screen::tft.drawString("Version: " + trimLines(v), 40, 30, 2);
        }

        // OK / Cancel
        Screen::tft.fillRoundRect(20, 60, 80, 40, 8, TFT_GREEN);
        Screen::tft.drawString("OK", 40, 70, 2);
        Screen::tft.fillRoundRect(120, 60, 80, 40, 8, TFT_RED);
        Screen::tft.drawString("Cancel", 130, 70, 2);

        String ok = readString("Enter 'y' to confirm: ");
        if (ok != "y")
            return;

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
