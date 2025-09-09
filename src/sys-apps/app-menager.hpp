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

    // Root CA certificate for onrender.co

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
        // if (s.length() == 0)
        //     return;
        // const char *cs = s.c_str();
        // int w = Screen::tft.textWidth(cs, font);
        // if (w <= maxW)
        // {
        //     Screen::tft.drawString(s, x, y, font);
        //     return;
        // }

        // // Add ellipsis for overflow text
        // String ellipsis = "...";
        // int ellipsisWidth = Screen::tft.textWidth(ellipsis.c_str(), font);

        // String displayText = s;
        // while (displayText.length() > 0 &&
        //        Screen::tft.textWidth(displayText.c_str(), font) + ellipsisWidth > maxW)
        // {
        //     displayText.remove(displayText.length() - 1);
        // }

        // displayText += ellipsis;
        Screen::tft.drawString(s, x, y, font);
    }

    // ---------- UI helpers ----------

    static void clearScreen(uint16_t color = BG)
    {
        Screen::tft.fillScreen(color);
    }

    static void drawTitle(const String &title)
    {
        Screen::tft.setTextColor(TEXT, BG);
        Screen::tft.drawString(title, 8, 8, 4);
    }

    static void drawMessage(const String &msg, int y, int font = 2, uint16_t fg = TEXT, uint16_t bg = BG)
    {
        Screen::tft.setTextColor(fg, bg);
        Screen::tft.drawString(msg, 8, y, font);
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
        Screen::tft.drawString(label, r.x + (r.w - Screen::tft.textWidth(label, 2)) / 2, r.y + (r.h - 16) / 2, 2);
    }

    static void drawError(const String &msg)
    {
        clearScreen(DANGER);
        Screen::tft.setTextColor(AT, DANGER);
        Screen::tft.drawString("Error:", 8, 8, 2);
        drawClippedString(8, 32, 304, 2, msg);
        Serial.println("[ERROR] " + msg);
        delay(2000);
    }

    static void drawSuccess(const String &msg)
    {
        clearScreen(PRIMARY);
        Screen::tft.setTextColor(AT, PRIMARY);
        drawClippedString(8, 8, 304, 2, msg);
        Serial.println("[SUCCESS] " + msg);
        delay(1500);
    }

    static void drawProgressBar(int x, int y, int width, int height, int progress, uint16_t color = PRIMARY)
    {
        // Draw border
        Screen::tft.drawRoundRect(x, y, width, height, 5, ACCENT2);

        // Calculate fill width
        int fillWidth = (progress * (width - 4)) / 100;

        // Draw progress
        if (fillWidth > 0)
        {
            Screen::tft.fillRoundRect(x + 2, y + 2, fillWidth, height - 4, 3, color);
        }

        // Draw percentage text
        String percent = String(progress) + "%";
        Screen::tft.setTextColor(TEXT, BG);
        Screen::tft.drawString(percent, x + (width - Screen::tft.textWidth(percent.c_str(), 2)) / 2,
                               y + (height - 16) / 2, 2);
    }

    // ---------- networking ----------

    static WiFiClientSecure secureClient; // global reusable
    static WiFiClient normalClient;

    static bool performGet(const String &url, Buffer &outBuf, bool useHttps)
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

        if (useHttps)
        {
            secureClient.setInsecure(); // <-- Hier wird das Zertifikat ignoriert
            if (!http.begin(secureClient, url))
            {
                drawError("http.begin failed");
                return false;
            }
        }
        else
        {
            if (!http.begin(normalClient, url))
            {
                drawError("http.begin failed");
                return false;
            }
        }

        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(10000);

        int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            Serial.printf("[ERROR] HTTP GET failed: %d\n", code);
            http.end();
            return false;
        }

        WiFiClient &stream = http.getStream();
        int len = http.getSize();
        Serial.printf("[HTTP] Content-Length: %d\n", len);

        if (len > 0)
        {
            outBuf.data.resize(len);
            size_t readLen = stream.readBytes(outBuf.data.data(), len);
            Serial.printf("[HTTP] Bytes read: %u\n", readLen);
            if (readLen != (size_t)len)
            {
                Serial.println("[ERROR] Stream read failed");
                http.end();
                return false;
            }
        }
        else
        {
            // Chunked/unknown size
            uint8_t tmp[512];
            while (stream.connected() || stream.available())
            {
                int r = stream.read(tmp, sizeof(tmp));
                if (r > 0)
                    outBuf.data.insert(outBuf.data.end(), tmp, tmp + r);
                else if (r < 0)
                {
                    Serial.println("[ERROR] Stream read error");
                    http.end();
                    return false;
                }
                delay(1);
            }
        }

        outBuf.ok = true;
        http.end();
        return true;
    }

    static bool performGetWithFallback(const String &url, Buffer &buf)
    {
        Serial.println("[Fallback] Trying HTTPS first...");
        if (performGet(url, buf, true))
            return true;

        // If HTTPS fails, try HTTP
        int p = url.indexOf("//");
        if (p >= 0)
        {
            String httpUrl = String("http://") + url.substring(p + 2);
            Serial.println("[Fallback] HTTPS failed, trying HTTP: " + httpUrl);
            return performGet(httpUrl, buf, false);
        }
        return false;
    }

    static bool fetchAndWrite(const String &url, const String &path, const String &folderName, bool required, int &progress, int totalFiles, int currentFile)
    {
        Serial.println("[Download] " + url + " -> " + path);

        // Update progress
        progress = (currentFile * 100) / totalFiles;
        clearScreen();
        drawTitle("Installing App");
        drawMessage("Downloading files...", 40);
        drawProgressBar(20, 80, 280, 30, progress);
        drawClippedString(20, 120, 280, 2, "Downloading: " + path);

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

        drawClippedString(35, 40, 277, 2, "Name: " + trimLines(appName));
        drawClippedString(35, 60, 277, 2, "Version: " + trimLines(version));

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

        clearScreen();
        drawTitle("Connecting to WiFi");
        drawMessage("Please wait...", 100);

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
            {base + "version.txt", "version.txt"}};

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
                drawTitle("Installing App");
                drawMessage("Downloading additional files...", 40);
                drawProgressBar(20, 80, 280, 30, progress);
                drawClippedString(20, 120, 280, 2, "Downloading: " + f);
            }
        }

        // Final progress
        clearScreen();
        drawTitle("Installing App");
        drawMessage("Finalizing installation...", 40);
        drawProgressBar(20, 80, 280, 30, 100);
        delay(500);

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
        drawTitle("Enter App ID");
        drawMessage("Please enter the App ID", 60);
        drawMessage("on the serial monitor", 80);

        String appId = readString("App ID: ");
        appId.trim();
        if (appId.length() == 0)
        {
            drawError("No App ID entered");
            return;
        }

        clearScreen();
        drawTitle("Preparing Installation");
        drawMessage("Please wait...", 100);

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