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

    // ---------- UI helpers ----------

    static void clearScreen(uint16_t color = BG)
    {
        Screen::tft.fillScreen(color);
    }

    static void drawTitle(const String &title)
    {
        Screen::tft.setTextColor(TEXT, BG);
        drawClippedString(8, 8, 320 - 16, 4, title);
    }

    static void drawMessage(const String &msg, int y, int font = 2, uint16_t fg = TEXT, uint16_t bg = BG)
    {
        Screen::tft.setTextColor(fg, bg);
        drawClippedString(8, y, 320 - 16, font, msg);
    }

    struct BtnRect
    {
        int x, y, w, h;
        bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
    };

    static void drawButton(const BtnRect &r, const char *label, uint16_t bg = PRIMARY, uint16_t fg = AT)
    {
        Screen::tft.fillRoundRect(r.x, r.y, r.w, r.h, 10, bg);
        Screen::tft.drawRoundRect(r.x, r.y, r.w, r.h, 10, ACCENT2);
        Screen::tft.setTextColor(fg, bg);
        drawClippedString(r.x + 6, r.y + (r.h - 16) / 2, r.w - 12, 2, String(label));

        // Also show label below button
        Screen::tft.setTextColor(TEXT, BG);
        drawClippedString(r.x, r.y + r.h + 4, r.w, 1, String(label));
    }

    static void drawError(const String &msg)
    {
        clearScreen(DANGER);
        Screen::tft.setTextColor(AT, DANGER);
        drawClippedString(8, 8, 320 - 16, 2, "Error:");
        drawClippedString(8, 32, 320 - 16, 2, msg);
        delay(2000);
    }

    static void drawSuccess(const String &msg)
    {
        clearScreen(PRIMARY);
        Screen::tft.setTextColor(AT, PRIMARY);
        drawClippedString(8, 8, 320 - 16, 2, msg);
        delay(1500);
    }

    // ---------- networking ----------

    static WiFiClientSecure secureClient; // global reusable
    static WiFiClient normalClient;

    static bool performGet(const String &url, Buffer &outBuf, bool useHttps)
    {
        const size_t MAX_BYTES = 200 * 1024;
        outBuf.data.clear();
        outBuf.ok = false;

        if (WiFi.status() != WL_CONNECTED)
        {
            drawError("WiFi not connected");
            return false;
        }

        HTTPClient http;
        WiFiClient *clientPtr = nullptr;

        if (useHttps)
        {
            secureClient.setInsecure(); // keine Zertifikatsprüfung
            clientPtr = &secureClient;
        }
        else
        {
            clientPtr = &normalClient;
        }

        if (!http.begin(*clientPtr, url))
        {
            drawError("http.begin failed");
            return false;
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            drawError("HTTP GET failed");
            http.end();
            return false;
        }

        WiFiClient &stream = http.getStream();
        size_t len = http.getSize();

        if (len <= 0 || len > MAX_BYTES)
        {
            drawError("Invalid body size");
            http.end();
            return false;
        }

        outBuf.data.resize(len);
        size_t readLen = stream.readBytes(outBuf.data.data(), len);
        if (readLen != len)
        {
            drawError("Stream read failed");
            http.end();
            return false;
        }

        outBuf.ok = true;
        http.end();
        return true;
    }

    static bool performGetWithFallback(const String &url, Buffer &buf)
    {
        if (performGet(url, buf, true))
            return true;
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
            if (required)
                drawError("Failed to download " + path);
            return !required;
        }

        if (!ENC_FS::exists({"programs"}))
            ENC_FS::mkDir({"programs"});
        if (!ENC_FS::exists({"programs", folderName}))
            ENC_FS::mkDir({"programs", folderName});

        bool written = ENC_FS::writeFile({"programs", folderName, path}, 0, 0, dataBuf.data);
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
        drawTitle("Install App?");

        safePush20x20Icon(8, 40, iconBuf);

        drawMessage("Name: " + trimLines(appName), 40, 2);
        drawMessage("Version: " + trimLines(version), 60, 2);

        BtnRect yes{30, 160, 110, 50};
        BtnRect no{180, 160, 110, 50};

        drawButton(yes, "Install", ACCENT, AT);
        drawButton(no, "Cancel", DANGER, AT);

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
        drawError("WiFi not connected");
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

        String name = nameBuf.ok ? String((const char *)nameBuf.data.data(), nameBuf.data.size()) : "Unknown";
        String version = verBuf.ok ? String((const char *)verBuf.data.data(), verBuf.data.size()) : "?";

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
        clearScreen();
        drawTitle("App Manager");

        BtnRect installRect{30, 80, 120, 44};
        BtnRect cancelRect{170, 80, 120, 44};

        drawButton(installRect, "Install new app", ACCENT3, AT);
        drawButton(cancelRect, "Cancel", DANGER, AT);

        char choice = waitForTwoButtonChoice(installRect, cancelRect);
        if (choice != 'i')
            return;

        clearScreen();
        drawMessage("Enter App ID on serial", 80);
        String appId = readString("App ID: ");
        appId.trim();
        if (appId.length() == 0)
        {
            drawError("No App ID entered");
            return;
        }

        clearScreen();
        drawMessage("Preparing...", 100);

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
