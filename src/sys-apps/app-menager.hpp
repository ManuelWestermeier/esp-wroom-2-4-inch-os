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
    }

    // ---------- networking ----------

    static bool performGet(const String &url, Buffer &outBuf, bool useHttps)
    {
        const size_t MAX_BYTES = 200 * 1024;
        outBuf.data.clear();
        outBuf.ok = false;

        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("performGet: WiFi not connected");
            return false;
        }

        HTTPClient http;

        WiFiClient *clientPtr = nullptr;
        if (useHttps)
        {
            WiFiClientSecure *client = new WiFiClientSecure();
            client->setCACert(nullptr);
            client->setInsecure();
            clientPtr = client;
        }
        else
        {
            clientPtr = new WiFiClient();
        }

        if (!http.begin(*clientPtr, url))
        {
            Serial.printf("http.begin failed: %s\n", url.c_str());
            delete clientPtr;
            return false;
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            Serial.printf("HTTP GET failed: code=%d for %s\n", code, url.c_str());
            http.end();
            delete clientPtr;
            return false;
        }

        String body = http.getString();
        if (body.length() > 0)
        {
            size_t len = (size_t)body.length();
            if (len > MAX_BYTES)
            {
                Serial.printf("Body length %u too large\n", (unsigned)len);
                http.end();
                delete clientPtr;
                return false;
            }
            outBuf.data.resize(len);
            memcpy(outBuf.data.data(), (const char *)body.c_str(), len);
            outBuf.ok = true;
        }

        http.end();
        delete clientPtr;
        return outBuf.ok;
    }

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

    static bool fetchAndWrite(const String &url, const String &path, const String &folderName, bool required)
    {
        Buffer dataBuf;
        if (!performGetWithFallback(url, dataBuf))
        {
            Serial.printf("Failed to download %s\n", url.c_str());
            return !required;
        }

        if (!ENC_FS::exists({"programs"}))
            ENC_FS::mkDir({"programs"});
        if (!ENC_FS::exists({"programs", folderName}))
            ENC_FS::mkDir({"programs", folderName});

        bool written = ENC_FS::writeFile({"programs", folderName, path}, 0, 0, dataBuf.data);
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

    static void safePush20x20Icon(int x, int y, const Buffer &buf)
    {
        const size_t ICON_PIX = 20 * 20;
        if (!buf.ok || buf.data.size() < 4)
            return;
        const uint8_t *p = buf.data.data() + 4;
        uint16_t tmp[ICON_PIX];
        for (size_t i = 0; i < ICON_PIX; ++i)
        {
            size_t off = i * 2;
            if (off + 1 < buf.data.size() - 4)
                tmp[i] = (uint16_t)p[off] | ((uint16_t)p[off + 1] << 8);
            else
                tmp[i] = 0;
        }
        Screen::tft.pushImage(x, y, 20, 20, tmp);
    }

    struct BtnRect
    {
        int x, y, w, h;
        bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    };

    static void drawButton(const BtnRect &r, const char *label, uint16_t bg = TFT_BLUE, uint16_t fg = TFT_WHITE)
    {
        Screen::tft.fillRoundRect(r.x, r.y, r.w, r.h, 8, bg);
        Screen::tft.setTextColor(fg, bg);
        drawClippedString(r.x + 6, r.y + (r.h - 16) / 2, r.w - 12, 2, String(label));
    }

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
        const int LEFT = 8;
        const int TOP = 8;
        const int AVAIL_W = 320 - LEFT * 2;

        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        drawClippedString(LEFT, TOP, AVAIL_W, 4, "Install App?");

        safePush20x20Icon(LEFT, TOP + 28, iconBuf);

        drawClippedString(LEFT + 28, TOP + 28, AVAIL_W - 28, 2, "Name: " + trimLines(appName));
        drawClippedString(LEFT + 28, TOP + 48, AVAIL_W - 28, 2, "Version: " + trimLines(version));

        BtnRect yes{LEFT + 20, 160, 110, 50};
        BtnRect no{LEFT + 160, 160, 110, 50};
        drawButton(yes, "Install", TFT_GREEN, TFT_BLACK);
        drawButton(no, "Cancel", TFT_RED, TFT_BLACK);

        char c = waitForTwoButtonChoice(yes, no);
        return (c == 'i');
    }

    static bool ensureWiFiConnected(unsigned long timeoutMs = 8000)
    {
        if (WiFi.status() == WL_CONNECTED)
            return true;
        unsigned long start = millis();
        while (millis() - start < timeoutMs)
        {
            if (WiFi.status() == WL_CONNECTED)
                return true;
            delay(100);
        }
        return false;
    }

    static bool installApp(const String &rawAppId)
    {
        if (!ensureWiFiConnected(10000))
            return false;

        String base = rawAppId;
        if (!rawAppId.startsWith("http://") && !rawAppId.startsWith("https://"))
            base = "https://" + rawAppId + ".onrender.com/";
        if (!base.endsWith("/"))
            base += "/";

        String folderName = sanitizeFolderName(rawAppId);

        Buffer nameBuf, verBuf, iconBuf;
        performGetWithFallback(base + "name.txt", nameBuf);
        performGetWithFallback(base + "version.txt", verBuf);
        performGetWithFallback(base + "icon-20x20.raw", iconBuf);

        String name = nameBuf.ok ? String((const char *)nameBuf.data.data(), nameBuf.data.size()) : "";
        String version = verBuf.ok ? String((const char *)verBuf.data.data(), verBuf.data.size()) : "";

        if (!confirmInstallPrompt(name, iconBuf, version))
            return false;

        // core files
        std::vector<std::pair<String, String>> core = {
            {base + "entry.lua", "entry.lua"},
            {base + "icon-20x20.raw", "icon-20x20.raw"},
            {base + "name.txt", "name.txt"},
            {base + "version.txt", "version.txt"}};

        for (auto &p : core)
        {
            if (!fetchAndWrite(p.first, p.second, folderName, true))
                return false;
        }

        // optional pkg.txt
        Buffer pkg;
        if (!performGetWithFallback(base + "pkg.txt", pkg))
            return true;
        auto extras = parsePkgTxt(pkg);
        for (auto &f : extras)
        {
            if (performGetWithFallback(base + f, pkg))
                ENC_FS::writeFile({"programs", folderName, f}, 0, 0, pkg.data);
        }

        return true;
    }

    static void showInstaller()
    {
        Screen::tft.fillScreen(TFT_BLACK);
        Screen::tft.setTextColor(TFT_WHITE, TFT_BLACK);
        drawClippedString(8, 8, 320 - 16, 4, "App Manager");

        BtnRect installRect{16, 48, 140, 44};
        BtnRect cancelRect{172, 48, 140, 44};
        drawButton(installRect, "Install new app", TFT_GREEN, TFT_BLACK);
        drawButton(cancelRect, "Cancel", TFT_RED, TFT_BLACK);

        char choice = waitForTwoButtonChoice(installRect, cancelRect);
        if (choice != 'i')
            return;

        Screen::tft.fillScreen(TFT_BLACK);
        drawClippedString(8, 8, 320 - 16, 2, "Enter App ID on serial");
        String appId = readString("App ID: ");
        appId.trim();
        if (appId.length() == 0)
            return;

        Screen::tft.fillScreen(TFT_BLACK);
        drawClippedString(8, 8, 320 - 16, 2, "Preparing...");

        bool res = installApp(appId);

        Screen::tft.fillScreen(res ? TFT_GREEN : TFT_RED);
        Screen::tft.setTextColor(TFT_BLACK, res ? TFT_GREEN : TFT_RED);
        drawClippedString(8, 8, 320 - 16, 2, res ? "Installed" : "Install failed");
        delay(1200);
    }

} // namespace AppManager

inline void appManager()
{
    AppManager::showInstaller();
}
