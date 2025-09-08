#pragma once
// file: app-manager.hpp
// App Manager UI for installing apps from appid.duckdns.org
// Uses readString() for input and ENC_FS for storage.

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <vector>

#include "../screen/index.hpp"   // Screen::tft, Screen::getTouchPos()
#include "../io/read-string.hpp" // readString(prompt, initial)
#include "../fs/enc-fs.hpp"
#include "../styles/global.hpp"

using std::vector;

namespace AppManager
{
    using Path = ENC_FS::Path;
    using Buffer = ENC_FS::Buffer;

    // Simple helper: write a buffer into ENC_FS at path (creates parent dirs)
    static bool writeBufferToPath(const Path &p, const Buffer &data)
    {
        // ensure parent dirs exist
        if (p.size() <= 1)
            ENC_FS::mkDir(p);
        else
        {
            Path parent = p;
            parent.pop_back();
            // create every segment along the way
            Path accu;
            for (size_t i = 0; i < parent.size(); ++i)
            {
                accu.push_back(parent[i]);
                ENC_FS::mkDir(accu);
            }
        }

        return ENC_FS::writeFile(p, 0, -1, data);
    }

    // Helper: converts ENC_FS::Buffer to Arduino String (safe)
    static String bufferToString(const Buffer &b)
    {
        if (b.empty())
            return String("");
        // make a null-terminated copy
        std::vector<uint8_t> tmp(b.begin(), b.end());
        tmp.push_back('\0');
        return String((const char *)tmp.data());
    }

    // HTTP GET an URL into buffer. Returns true on success (200) and fills out buffer.
    static bool httpGetToBuffer(const String &url, Buffer &out)
    {
        HTTPClient http;
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        if (!http.begin(url))
        {
            Serial.printf("http begin failed for %s\n", url.c_str());
            return false;
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK)
        {
            Serial.printf("http GET %s -> %d\n", url.c_str(), code);
            http.end();
            return false;
        }

        WiFiClient *stream = http.getStreamPtr();
        int contentLength = http.getSize();

        out.clear();
        if (contentLength > 0)
        {
            out.reserve(contentLength);
            while (http.connected() && (contentLength > 0))
            {
                size_t available = stream->available();
                if (available)
                {
                    size_t toRead = std::min((size_t)available, (size_t)512);
                    std::vector<uint8_t> buf(toRead);
                    int r = stream->readBytes((char *)buf.data(), toRead);
                    if (r > 0)
                        out.insert(out.end(), buf.begin(), buf.begin() + r);
                    contentLength -= r;
                }
                else
                {
                    delay(5);
                }
            }
        }
        else
        {
            // unknown length -> read until end
            while (http.connected())
            {
                while (stream->available())
                {
                    uint8_t chunk[512];
                    int r = stream->readBytes((char *)chunk, sizeof(chunk));
                    if (r > 0)
                        out.insert(out.end(), chunk, chunk + r);
                }
                if (!http.connected())
                    break;
                delay(5);
            }
        }

        http.end();
        return true;
    }

    // Ensure programs/<appId> directory exists
    static bool ensureAppDir(const String &appId)
    {
        Path p;
        p.push_back("programs");
        p.push_back(appId);
        return ENC_FS::mkDir(p);
    }

    // Write a file fetched from baseUrl + "/" + remotePath into programs/<appId>/<filename or remotePath>
    static bool fetchAndWrite(const String &baseUrl, const String &remotePath, const String &appId, bool isBinary = true)
    {
        String url = baseUrl;
        if (!url.endsWith("/"))
            url += "/";
        // remotePath may start with '/'
        String rp = remotePath;
        while (rp.startsWith("/"))
            rp.remove(0, 1);
        url += rp;

        ENC_FS::Buffer data;
        if (!httpGetToBuffer(url, data))
        {
            Serial.printf("Failed to download %s\n", url.c_str());
            return false;
        }

        // build target path: programs / appId / (remotePath last segment or full remotePath)
        // If remotePath contains directories we create them and keep the same structure under app dir.
        Path target;
        target.push_back("programs");
        target.push_back(appId);

        // split rp by '/'
        int start = 0;
        int len = rp.length();
        for (int i = 0; i <= len; ++i)
        {
            if (i == len || rp.charAt(i) == '/')
            {
                if (i - start > 0)
                {
                    String seg = rp.substring(start, i);
                    target.push_back(seg);
                }
                start = i + 1;
            }
        }

        return writeBufferToPath(target, data);
    }

    // Trim CR/LF and whitespace from String (in place)
    static String trimLines(String s)
    {
        // remove leading/trailing whitespace
        int a = 0;
        while (a < s.length() && isspace((unsigned char)s[a]))
            a++;
        int b = s.length() - 1;
        while (b >= a && isspace((unsigned char)s[b]))
            b--;
        if (b < a)
            return String("");
        return s.substring(a, b + 1);
    }

    // Parse pkg.txt contents (raw buffer) into vector of relative paths
    static vector<String> parsePkgTxt(const Buffer &buf)
    {
        vector<String> out;
        String s = bufferToString(buf);
        int idx = 0;
        while (idx < s.length())
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

    // High level installer: given appId, fetch the core files and pkg.txt extras
    // Returns true if at least the core files were installed (best-effort for extras)
    static bool installApp(const String &appId)
    {
        if (appId.length() == 0)
            return false;

        if (WiFi.status() != WL_CONNECTED)
        {
            Serial.println("WiFi not connected");
            return false;
        }

        String base = String("http://") + appId + String(".duckdns.org");

        // ensure directory
        ensureAppDir(appId);

        bool okEntry = fetchAndWrite(base, "entry.lua", appId);
        bool okIcon = fetchAndWrite(base, "icon-20x20.raw", appId);
        bool okName = fetchAndWrite(base, "name.txt", appId, false);
        bool okVersion = fetchAndWrite(base, "version.txt", appId, false);

        // try pkg.txt
        ENC_FS::Buffer pkgBuf;
        bool gotPkg = httpGetToBuffer(base + "/pkg.txt", pkgBuf);
        if (gotPkg)
        {
            vector<String> extras = parsePkgTxt(pkgBuf);
            for (auto &rp : extras)
            {
                // skip empty/comment lines
                if (rp.length() == 0)
                    continue;
                // fetch and write each path
                bool r = fetchAndWrite(base, rp, appId);
                if (!r)
                    Serial.printf("Failed extra: %s\n", rp.c_str());
            }
        }

        return okEntry || okName || okVersion || okIcon;
    }

    // Simple UI: prompt for app id using readString(), then install while showing progress on the screen.
    // Returns true if installation succeeded (at least partially).
    static bool appManagerUI()
    {
        auto &tft = Screen::tft;
        tft.fillScreen(BG);

        // draw a simple header
        tft.setTextDatum(TC_DATUM);
        tft.setTextSize(2);
        tft.setTextColor(TEXT, BG);
        tft.drawString("App Manager", 160, 12, 4);

        // prompt for app id using existing readString helper
        String appId = readString("Install app id (example: myapp)", "");
        if (appId.length() == 0)
        {
            // cancelled or empty
            tft.setTextSize(1);
            tft.setTextDatum(TC_DATUM);
            tft.drawString("Cancelled", 160, 120, 2);
            delay(700);
            return false;
        }

        // show installing message
        tft.fillRect(10, 48, 300, 160, BG);
        tft.setTextDatum(TL_DATUM);
        tft.setTextSize(1);
        tft.setTextColor(TEXT, BG);
        tft.drawString(String("Installing: ") + appId, 16, 64, 2);

        unsigned long start = millis();
        bool res = installApp(appId);
        unsigned long dur = millis() - start;

        // show result
        tft.fillRect(10, 92, 300, 100, BG);
        tft.setTextDatum(TC_DATUM);
        tft.setTextSize(1);
        if (res)
        {
            tft.setTextColor(TEXT, BG);
            tft.drawString("Install finished", 160, 120, 2);
            tft.drawString(String("Time: ") + String(dur / 1000.0, 2) + "s", 160, 140, 2);
        }
        else
        {
            tft.setTextColor(TEXT, BG);
            tft.drawString("Install failed", 160, 120, 2);
            tft.drawString(String("Time: ") + String(dur / 1000.0, 2) + "s", 160, 140, 2);
        }

        delay(1200);
        return res;
    }

} // namespace AppManager

// Public wrapper
static inline bool appManager()
{
    return AppManager::appManagerUI();
}
