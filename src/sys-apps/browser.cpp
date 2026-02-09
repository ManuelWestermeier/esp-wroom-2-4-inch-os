// File: browser.cpp
// Combined browser implementation: merges working LittleFS-based storage & menu (from the
// simple WiFiClient example) with the production-ready WebSocket-driven renderer and viewport/touch
// handling. This file assumes the rest of your project provides:
// - Screen::tft and Screen::getTouchPos() (screen/index.hpp)
// - readString(question, default) (io/read-string.hpp)
// - Style::Colors (styles/global.hpp)
// - base64EncodeSafe(const String&) (browser/base64encode.hpp)
// - LittleFS is initialized elsewhere (LittleFS.begin())
// Adjust includes/paths if your project structure differs.

#include "browser.hpp"

#include <Arduino.h>
#include <WiFi.h>
#include <WebSocketsClient.h>
#include <LittleFS.h>
#include <SPI.h>

#include "../screen/index.hpp"
#include "../io/read-string.hpp"
#include "../styles/global.hpp"

#include "nanosvg.h"

String base64EncodeSafe(const String &input)
{
    const char *base64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String encoded = "";

    int i = 0;
    int inputLen = input.length();
    unsigned char char_array_3[3];
    unsigned char char_array_4[4];

    for (int pos = 0; pos < inputLen; ++pos)
    {
        char_array_3[i++] = input[pos];
        if (i == 3)
        {
            char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
            char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
            char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
            char_array_4[3] = char_array_3[2] & 0x3f;

            for (i = 0; i < 4; i++)
                encoded += base64_chars[char_array_4[i]];
            i = 0;
        }
    }

    if (i)
    {
        for (int j = i; j < 3; j++)
            char_array_3[j] = '\0';

        char_array_4[0] = (char_array_3[0] & 0xfc) >> 2;
        char_array_4[1] = ((char_array_3[0] & 0x03) << 4) + ((char_array_3[1] & 0xf0) >> 4);
        char_array_4[2] = ((char_array_3[1] & 0x0f) << 2) + ((char_array_3[2] & 0xc0) >> 6);
        char_array_4[3] = char_array_3[2] & 0x3f;

        for (int j = 0; j < i + 1; j++)
            encoded += base64_chars[char_array_4[j]];

        // Padding characters, not needed for filenames
        // encoded += String((i == 1) ? "==" : "=");
    }

    // Remove unsafe characters for filesystem
    encoded.replace("+", "-");
    encoded.replace(String("/"), "_");
    encoded.replace("=", "");

    return encoded;
}

namespace Browser
{
    // --- State & constants (match browser.hpp) ---
    Location loc;
    bool isRunning = false;
    WebSocketsClient webSocket;
    String Location::sessionId = String(random(100000, 999999));

    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 240;
    static constexpr int TOPBAR_H = 20;
    static constexpr int BUTTON_PADDING = 10;
    static constexpr int BUTTON_H = 36;
    static constexpr int VISIT_LIST_Y = 100;
    static constexpr int VISIT_ITEM_H = 30;
    static constexpr int VIEWPORT_Y = TOPBAR_H;
    static constexpr int VIEWPORT_H = SCREEN_H - TOPBAR_H;

    static constexpr uint16_t CLICK_TOLERANCE = 12;
    static constexpr uint16_t STABILIZE_MS = 30;

    // Theme using project style colors
    static struct Theme
    {
        uint16_t bg = Style::Colors::bg;
        uint16_t text = Style::Colors::text;
        uint16_t primary = Style::Colors::primary;
        uint16_t accent = Style::Colors::accent;
        uint16_t accent2 = Style::Colors::accent2;
        uint16_t accentText = Style::Colors::accentText;
        uint16_t pressed = Style::Colors::pressed;
        uint16_t danger = Style::Colors::danger;
        uint16_t placeholder = Style::Colors::placeholder;
    } theme;

    // touch/scroll state
    static int visitScrollOffset = 0;
    static bool touchActive = false;
    static bool dragging = false;
    static bool clicked = false;
    static int16_t startX = 0, startY = 0, lastX = 0, lastY = 0;
    static uint32_t touchStartMs = 0;
    static uint16_t moved = 0;

    // viewport
    static bool viewportActive = false;
    static int vpX = 0, vpY = 0, vpW = SCREEN_W, vpH = SCREEN_H;

    // recent inputs cache
    static std::vector<String> recentInputs;

    // ---------------------------
    // Simple browser storage using LittleFS
    // Path layout:
    //   /browser/storage/<base64(domain)>.data
    // Also provide listSites() by scanning directory.
    // ---------------------------
    namespace LocalStorage
    {
        static String toPath(const String &key)
        {
            return String("/browser/storage/") + base64EncodeSafe(key) + String(".data");
        }

        void ensureDir(const String &fullPath)
        {
            if (!fullPath.startsWith("/"))
                return;
            // make directories up to final dir
            String path = "";
            int from = 1;
            while (true)
            {
                int next = fullPath.indexOf('/', from);
                if (next == -1)
                    break;
                path = fullPath.substring(0, next);
                if (!LittleFS.exists(path))
                {
                    LittleFS.mkdir(path);
                }
                from = next + 1;
            }
            // ensure parent of file exists
            if (!fullPath.endsWith("/"))
            {
                int lastSlash = fullPath.lastIndexOf('/');
                if (lastSlash > 0)
                {
                    String parent = fullPath.substring(0, lastSlash);
                    if (!LittleFS.exists(parent))
                        LittleFS.mkdir(parent);
                }
            }
        }

        void set(const String &key, const String &val)
        {
            String p = toPath(key);
            ensureDir(p);
            File f = LittleFS.open(p, "w");
            if (!f)
                return;
            f.print(val);
            f.close();
        }

        String get(const String &key)
        {
            String p = toPath(key);
            if (!LittleFS.exists(p))
                return String();
            File f = LittleFS.open(p, "r");
            if (!f)
                return String();
            String s = f.readString();
            f.close();
            return s;
        }

        void del(const String &key)
        {
            String p = toPath(key);
            if (LittleFS.exists(p))
                LittleFS.remove(p);
        }

        void clearAll()
        {
            // naive recursive remove of /browser/storage
            if (LittleFS.exists("/browser/storage"))
            {
                // iterate files
                File root = LittleFS.open("/browser/storage", "r");
                File file;
                while (file = root.openNextFile())
                {
                    String name = String(file.name());
                    file.close();
                    LittleFS.remove(name);
                }
                root.close();
            }
        }

        std::vector<String> listSites()
        {
            std::vector<String> out;
            if (!LittleFS.exists("/browser/storage"))
                return out;
            File root = LittleFS.open("/browser/storage", "r");
            if (!root)
                return out;
            File file;
            while (file = root.openNextFile())
            {
                String name = String(file.name()); // e.g. /browser/storage/<b64>.data
                file.close();
                // strip folder and .data then decode base64 - but we don't implement decoding here,
                // instead return the base64 token; the UI will present it (or you can store domain plaintext as value).
                // To keep things simple we save the readable domain as the file content on saveVisitedSite.
                String content;
                File f = LittleFS.open(name, "r");
                if (f)
                {
                    content = f.readString();
                    f.close();
                    if (content.length())
                        out.push_back(content);
                    else
                        out.push_back(name); // fallback
                }
                else
                {
                    out.push_back(name);
                }
            }
            root.close();
            return out;
        }
    } // namespace LocalStorage

    // ---------------------------
    // Viewport helpers
    // ---------------------------
    static inline void enterViewport(int x, int y, int w, int h)
    {
        if (x < 0)
            x = 0;
        if (y < 0)
            y = 0;
        if (w <= 0)
            w = SCREEN_W;
        if (h <= 0)
            h = SCREEN_H;
        if (x + w > SCREEN_W)
            w = SCREEN_W - x;
        if (y + h > SCREEN_H)
            h = SCREEN_H - y;
        vpX = x;
        vpY = y;
        vpW = w;
        vpH = h;
        viewportActive = true;
        Screen::tft.setViewport(vpX, vpY, vpW, vpH);
        Serial.printf("[Browser] enterViewport x=%d y=%d w=%d h=%d\n", vpX, vpY, vpW, vpH);
    }

    static inline void exitViewport()
    {
        viewportActive = false;
        vpX = 0;
        vpY = 0;
        vpW = SCREEN_W;
        vpH = SCREEN_H;
        Screen::tft.setViewport(0, 0, SCREEN_W, SCREEN_H);
        Serial.println("[Browser] exitViewport");
    }

    // ---------------------------
    // Drawing helpers (viewport aware)
    // ---------------------------
    void drawText(int x, int y, const String &text, uint16_t color, int size)
    {
        int localW = viewportActive ? vpW : SCREEN_W;
        int localH = viewportActive ? vpH : SCREEN_H;
        if (y < 0 || y >= localH)
            return;
        Screen::tft.setTextSize(size);
        Screen::tft.setTextColor(color);
        Screen::tft.setCursor(x, y);
        int textWidth = text.length() * 6 * size;
        String display = text;
        if (x + textWidth > localW)
        {
            int maxChars = (localW - x) / (6 * size);
            if (maxChars > 3)
                display = text.substring(0, maxChars - 3) + "...";
            else
                return;
        }
        Screen::tft.print(display);
    }

    void drawCircle(int x, int y, int r, uint16_t color)
    {
        Screen::tft.drawCircle(x, y, r, color);
    }

    // Very small wrapper: parse & draw SVG via nanosvg helper (assumes implementation elsewhere)
    void drawSVG(const String &svgStr, int x, int y, int w, int h, uint16_t color)
    {
        // use nanosvg helpers from project (createSVG/drawSVGString). If not available, ignore.
        NSVGimage *img = createSVG(svgStr);
        if (img)
        {
            drawSVGString(svgStr, x, y, w, h, color);
        }
        else
        {
            Serial.println("[Browser] drawSVG: parse failed");
        }
    }

    // ---------------------------
    // Utility: clamp scroll offset
    // ---------------------------
    static inline void clampScrollForSites(const std::vector<String> &sites)
    {
        int total = (int)sites.size() * VISIT_ITEM_H;
        int visible = SCREEN_H - VISIT_LIST_Y;
        int maxOff = total > visible ? total - visible : 0;
        if (visitScrollOffset < 0)
            visitScrollOffset = 0;
        if (visitScrollOffset > maxOff)
            visitScrollOffset = maxOff;
    }

    // ---------------------------
    // Prompt & input helpers
    // ---------------------------
    String promptText(const String &question, const String &defaultValue)
    {
        ReRender();
        String res = readString(question, defaultValue);
        if (res.length())
        {
            recentInputs.insert(recentInputs.begin(), res);
            if (recentInputs.size() > 8)
                recentInputs.pop_back();
        }
        Serial.printf("[Browser] promptText '%s' -> '%s'\n", question.c_str(), res.c_str());
        return res;
    }

    // ---------------------------
    // Command handling (from server)
    // ---------------------------
    void handleCommand(const String &payload)
    {
        Serial.printf("[Browser] handleCommand payload='%s'\n", payload.c_str());

        if (payload.startsWith("FillRect"))
        {
            int x = 0, y = 0, w = 0, h = 0;
            unsigned int c = 0;
            if (sscanf(payload.c_str(), "FillRect %d %d %d %d %u", &x, &y, &w, &h, &c) == 5)
            {
                if (x < 0)
                {
                    w += x;
                    x = 0;
                }
                if (y < 0)
                {
                    h += y;
                    y = 0;
                }
                if (x + w > SCREEN_W)
                    w = SCREEN_W - x;
                if (y + h > SCREEN_H)
                    h = SCREEN_H - y;
                if (w > 0 && h > 0)
                    Screen::tft.fillRect(x, y, w, h, (uint16_t)c);
            }
            return;
        }

        if (payload.startsWith("DrawCircle"))
        {
            int x = 0, y = 0, r = 0;
            unsigned int c = 0;
            if (sscanf(payload.c_str(), "DrawCircle %d %d %d %u", &x, &y, &r, &c) == 4)
            {
                if (r > 0 && x >= -r && x <= SCREEN_W + r && y >= -r && y <= SCREEN_H + r)
                    drawCircle(x, y, r, (uint16_t)c);
            }
            return;
        }

        if (payload.startsWith("DrawText"))
        {
            int x = 0, y = 0, size = 1;
            unsigned int c = 0;
            char buf[512] = {0};
            if (sscanf(payload.c_str(), "DrawText %d %d %d %u %[^\n]", &x, &y, &size, &c, buf) >= 5)
            {
                String text = String(buf);
                drawText(x, y, text, (uint16_t)c, size);
            }
            return;
        }

        if (payload.startsWith("DrawSVG"))
        {
            int x = 0, y = 0, w = 0, h = 0;
            unsigned int c = 0;
            if (sscanf(payload.c_str(), "DrawSVG %d %d %d %d %u", &x, &y, &w, &h, &c) == 5)
            {
                // find header end (after 5 tokens)
                int countSpaces = 0, headerEnd = -1;
                for (int i = 0; i < (int)payload.length(); ++i)
                {
                    if (payload[i] == ' ')
                        ++countSpaces;
                    if (countSpaces == 5)
                    {
                        headerEnd = i;
                        break;
                    }
                }
                if (headerEnd >= 0 && headerEnd + 1 < (int)payload.length())
                {
                    String svg = payload.substring(headerEnd + 1);
                    if (x < 0)
                    {
                        w += x;
                        x = 0;
                    }
                    if (y < 0)
                    {
                        h += y;
                        y = 0;
                    }
                    if (x >= SCREEN_W || y >= SCREEN_H)
                        return;
                    if (x + w > SCREEN_W)
                        w = SCREEN_W - x;
                    if (y + h > SCREEN_H)
                        h = SCREEN_H - y;
                    if (w > 0 && h > 0)
                        drawSVG(svg, x, y, w, h, (uint16_t)c);
                }
            }
            return;
        }

        // Theme
        if (payload.startsWith("SetThemeColor"))
        {
            int idx = payload.indexOf(' ', 13);
            if (idx > 0)
            {
                String colorName = payload.substring(13, idx);
                String colorValue = payload.substring(idx + 1);
                if (colorValue.startsWith("0x"))
                    colorValue = colorValue.substring(2);
                uint16_t color = (uint16_t)strtoul(colorValue.c_str(), nullptr, 16);
                if (colorName == "bg")
                    theme.bg = color;
                else if (colorName == "text")
                    theme.text = color;
                else if (colorName == "primary")
                    theme.primary = color;
                else if (colorName == "accent")
                    theme.accent = color;
                else if (colorName == "accent2")
                    theme.accent2 = color;
                else if (colorName == "accentText")
                    theme.accentText = color;
                else if (colorName == "pressed")
                    theme.pressed = color;
                else if (colorName == "danger")
                    theme.danger = color;
                else if (colorName == "placeholder")
                    theme.placeholder = color;
                if (loc.state == "home" || loc.state == "website")
                    ReRender();
            }
            return;
        }

        if (payload.startsWith("GetThemeColor"))
        {
            String name = payload.substring(14);
            uint16_t c = 0xFFFF;
            if (name == "bg")
                c = theme.bg;
            else if (name == "text")
                c = theme.text;
            else if (name == "primary")
                c = theme.primary;
            else if (name == "accent")
                c = theme.accent;
            else if (name == "accent2")
                c = theme.accent2;
            else if (name == "accentText")
                c = theme.accentText;
            else if (name == "pressed")
                c = theme.pressed;
            else if (name == "danger")
                c = theme.danger;
            else if (name == "placeholder")
                c = theme.placeholder;
            webSocket.sendTXT("ThemeColor " + name + " " + String(c, HEX));
            return;
        }

        if (payload.startsWith("SetStorage"))
        {
            int idx = payload.indexOf(' ', 11);
            if (idx > 0)
            {
                String key = payload.substring(11, idx);
                String val = payload.substring(idx + 1);
                LocalStorage::set(key, val);
                Serial.printf("[Browser] SetStorage key='%s' len=%d\n", key.c_str(), (int)val.length());
            }
            return;
        }

        if (payload.startsWith("GetStorage"))
        {
            String key = payload.substring(11);
            String s = LocalStorage::get(key);
            webSocket.sendTXT("GetBackStorage " + key + " " + s);
            Serial.printf("[Browser] GetStorage key='%s' len=%d\n", key.c_str(), (int)s.length());
            return;
        }

        if (payload.startsWith("Navigate"))
        {
            String arg = payload.substring(9);
            arg.trim();
            if (arg == "home")
            {
                loc.state = "home";
                ReRender();
                return;
            }
            String domain = arg;
            int port = 443;
            String state = "startpage";
            int at = arg.indexOf('@');
            if (at >= 0)
            {
                domain = arg.substring(0, at);
                state = arg.substring(at + 1);
            }
            int colon = domain.indexOf(':');
            if (colon >= 0)
            {
                port = domain.substring(colon + 1).toInt();
                domain = domain.substring(0, colon);
            }
            navigate(domain, port, state);
            return;
        }

        if (payload.startsWith("Exit"))
        {
            Exit();
            return;
        }

        if (payload.startsWith("PromptText"))
        {
            int idx = payload.indexOf(' ', 11);
            String rid;
            if (idx > 0)
                rid = payload.substring(11, idx);
            else
                rid = payload.substring(11);
            ReRender();
            String input = promptText("Enter text:", "");
            // store last input
            LocalStorage::set("_last_input", input);
            if (input.length())
            {
                recentInputs.insert(recentInputs.begin(), input);
                if (recentInputs.size() > 8)
                    recentInputs.pop_back();
            }
            webSocket.sendTXT("GetBackText " + rid + " " + input);
            ReRender();
            return;
        }

        if (payload.startsWith("Title "))
        {
            loc.title = payload.substring(6);
            ReRender();
            return;
        }

        Serial.println("[Browser] Unhandled command");
    }

    // ---------------------------
    // WebSocket event wrapper
    // ---------------------------
    static void wsEvent(WStype_t type, uint8_t *payload, size_t length)
    {
        String msg = (payload != nullptr && length > 0) ? String((char *)payload) : String();
        switch (type)
        {
        case WStype_CONNECTED:
        {
            Serial.println("[Browser] WebSocket Connected");
            String sess = (loc.session.length() ? loc.session : Location::sessionId);
            webSocket.sendTXT("MWOSP-v1 " + sess + " " + String(SCREEN_W) + " " + String(SCREEN_H));
            // send theme
            String themeMsg = "ThemeColors";
            themeMsg += " bg:0x" + String(theme.bg, HEX);
            themeMsg += " text:0x" + String(theme.text, HEX);
            themeMsg += " primary:0x" + String(theme.primary, HEX);
            themeMsg += " accent:0x" + String(theme.accent, HEX);
            themeMsg += " accent2:0x" + String(theme.accent2, HEX);
            themeMsg += " accentText:0x" + String(theme.accentText, HEX);
            themeMsg += " pressed:0x" + String(theme.pressed, HEX);
            themeMsg += " danger:0x" + String(theme.danger, HEX);
            themeMsg += " placeholder:0x" + String(theme.placeholder, HEX);
            webSocket.sendTXT(themeMsg);
            Serial.println("[Browser] Sent ThemeColors");
            break;
        }
        case WStype_TEXT:
            Serial.printf("[Browser] WebSocket TEXT: %s\n", msg.c_str());
            handleCommand(msg);
            break;
        case WStype_DISCONNECTED:
            Serial.println("[Browser] WebSocket Disconnected");
            break;
        default:
            Serial.printf("[Browser] WebSocket Event type=%d\n", type);
            break;
        }
    }

    // ---------------------------
    // Navigation & visited list
    // ---------------------------
    void saveVisitedSite(const String &domain)
    {
        unsigned long ts = millis();
        String s = domain; // store readable domain as file content
        LocalStorage::set(domain, s);
        Serial.printf("[Browser] saveVisitedSite '%s'\n", domain.c_str());
    }

    void navigate(const String &domain, int port, const String &state)
    {
        Serial.printf("[Browser] navigate domain='%s' port=%d state='%s'\n", domain.c_str(), port, state.c_str());
        loc.domain = domain;
        loc.port = port;
        loc.state = state;
        saveVisitedSite(domain);
        webSocket.disconnect();
        delay(10);
        // open secure websocket
        webSocket.beginSSL(loc.domain.c_str(), loc.port, "/");
        webSocket.setReconnectInterval(5000);
        ReRender();
    }

    // ---------------------------
    // UI rendering: topbar, home, list, website
    // ---------------------------
    void renderTopBar()
    {
        Screen::tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, theme.primary);
        drawText(6, 3, loc.title.length() ? loc.title : "Title Home", theme.text, 2);
        drawText(SCREEN_W - 48, 3, "Exit", theme.danger, 2);
    }

    void showHomeUI()
    {
        int cardX = 6, cardY = 25, cardW = SCREEN_W - cardX * 2, cardH = 48;
        Screen::tft.fillRoundRect(cardX, cardY, cardW, cardH, 6, theme.primary);
        Screen::tft.fillRoundRect(cardX + 2, cardY + 2, cardW - 4, cardH - 4, 6, theme.bg);
        int btnW = (cardW - BUTTON_PADDING * 3) / 2;
        int bx = cardX + BUTTON_PADDING, by = cardY + 6;
        Screen::tft.fillRoundRect(bx, by, btnW, BUTTON_H, 6, theme.accent);
        drawText(bx + 8, by + 8, "Open Site", theme.accentText, 2);
        bx += btnW + BUTTON_PADDING;
        Screen::tft.fillRoundRect(bx, by, btnW, BUTTON_H, 6, theme.accent2);
        drawText(bx + 8, by + 8, "Open Search", theme.accentText, 2);
    }

    void showVisitedSites()
    {
        std::vector<String> sites = LocalStorage::listSites();
        drawText(10, VISIT_LIST_Y - 18, "Visited Sites", theme.text, 2);

        enterViewport(0, VISIT_LIST_Y, SCREEN_W, SCREEN_H - VISIT_LIST_Y);

        clampScrollForSites(sites);

        int xText = 10;
        for (size_t i = 0; i < sites.size(); ++i)
        {
            int localY = (int)i * VISIT_ITEM_H - visitScrollOffset;
            if (localY + VISIT_ITEM_H < 0)
                continue;
            if (localY > vpH)
                continue;
            uint16_t bgColor = (i % 2 == 0) ? theme.primary : theme.bg;
            int drawY = localY;
            int drawH = VISIT_ITEM_H - 2;
            if (drawY < 0)
            {
                drawH += drawY;
                drawY = 0;
            }
            if (drawY + drawH > vpH)
                drawH = vpH - drawY;
            if (drawH > 0)
                Screen::tft.fillRect(0, drawY, vpW, drawH, bgColor);
            String domain = sites[i];
            int maxDomainWidth = vpW - 180;
            if (domain.length() > maxDomainWidth / 6)
                domain = domain.substring(0, maxDomainWidth / 6 - 3) + "...";
            int textY = localY + 6;
            if (textY < 0)
                textY = 0;
            drawText(xText, textY, domain, theme.text, 1);
            int btnW = 56, btnGap = 4;
            int xOpen = vpW - BUTTON_PADDING - btnW;
            int xClear = xOpen - btnGap - btnW;
            int xDelete = xClear - btnGap - btnW;
            int btnY = localY + 4;
            int btnH = VISIT_ITEM_H - 10;
            if (btnY < 0)
            {
                int visibleBtnH = btnH + btnY;
                if (visibleBtnH > 0)
                {
                    Screen::tft.fillRoundRect(xDelete, 0, btnW, visibleBtnH, 4, theme.danger);
                    drawText(xDelete + 8, 2, "Delete", theme.accentText, 1);
                    Screen::tft.fillRoundRect(xClear, 0, btnW, visibleBtnH, 4, theme.pressed);
                    drawText(xClear + 6, 2, "Clear", theme.accentText, 1);
                    Screen::tft.fillRoundRect(xOpen, 0, btnW, visibleBtnH, 4, theme.accent);
                    drawText(xOpen + 10, 2, "Open", theme.accentText, 1);
                }
            }
            else if (btnY + btnH > vpH)
            {
                int visibleBtnH = vpH - btnY;
                if (visibleBtnH > 0)
                {
                    Screen::tft.fillRoundRect(xDelete, btnY, btnW, visibleBtnH, 4, theme.danger);
                    drawText(xDelete + 8, btnY + 2, "Delete", theme.accentText, 1);
                    Screen::tft.fillRoundRect(xClear, btnY, btnW, visibleBtnH, 4, theme.pressed);
                    drawText(xClear + 6, btnY + 2, "Clear", theme.accentText, 1);
                    Screen::tft.fillRoundRect(xOpen, btnY, btnW, visibleBtnH, 4, theme.accent);
                    drawText(xOpen + 10, btnY + 2, "Open", theme.accentText, 1);
                }
            }
            else
            {
                Screen::tft.fillRoundRect(xDelete, btnY, btnW, btnH, 4, theme.danger);
                drawText(xDelete + 8, btnY + 6, "Delete", theme.accentText, 1);
                Screen::tft.fillRoundRect(xClear, btnY, btnW, btnH, 4, theme.pressed);
                drawText(xClear + 6, btnY + 6, "Clear", theme.accentText, 1);
                Screen::tft.fillRoundRect(xOpen, btnY, btnW, btnH, 4, theme.accent);
                drawText(xOpen + 10, btnY + 6, "Open", theme.accentText, 1);
            }
        }

        exitViewport();
    }

    void showWebsitePage()
    {
        Screen::tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, theme.primary);
        String title = loc.title.length() ? loc.title : loc.domain;
        if (title.length() > 20)
            title = title.substring(0, 17) + "...";
        drawText(6, 3, title, theme.accentText, 2);
        drawText(SCREEN_W - 22, 3, "X", theme.danger, 2);
        enterViewport(0, VIEWPORT_Y, SCREEN_W, VIEWPORT_H);
        Screen::tft.fillRect(0, 0, vpW, vpH, theme.bg);
        drawText(6, 4, "Page view", theme.placeholder, 1);
        exitViewport();
    }

    // ---------------------------
    // ReRender / lifecycle
    // ---------------------------
    void ReRender()
    {
        Serial.printf("[Browser] ReRender state='%s' domain='%s'\n", loc.state.c_str(), loc.domain.c_str());
        Screen::tft.fillScreen(theme.bg);
        renderTopBar();
        if (loc.state == "home" || loc.state == "")
        {
            showHomeUI();
            showVisitedSites();
        }
        else
        {
            showWebsitePage();
        }
    }

    void Start()
    {
        Serial.println("[Browser] Start");
        Screen::tft.fillScreen(theme.bg);
        renderTopBar();
        showHomeUI();
        showVisitedSites();

        webSocket.onEvent(wsEvent);
        isRunning = true;
    }

    void OnExit()
    {
        Serial.println("[Browser] OnExit - disconnecting websocket");
        webSocket.disconnect();
    }

    void Exit()
    {
        Serial.println("[Browser] Exit");
        isRunning = false;
        OnExit();
        loc.state = "home";
        ReRender();
    }

    // ---------------------------
    // Touch handling
    // ---------------------------
    static inline void onTouchStart(int16_t x, int16_t y)
    {
        touchActive = true;
        dragging = false;
        clicked = false;
        startX = lastX = x;
        startY = lastY = y;
        moved = 0;
        touchStartMs = millis();
    }

    static inline void onTouchMove(int16_t x, int16_t y)
    {
        if (!touchActive)
            return;
        if (millis() - touchStartMs < STABILIZE_MS)
        {
            lastX = x;
            lastY = y;
            return;
        }
        moved = abs(x - startX) + abs(y - startY);
        if (moved > CLICK_TOLERANCE)
            dragging = true;
        lastX = x;
        lastY = y;
    }

    static inline void onTouchEnd()
    {
        if (!touchActive)
            return;
        if (!dragging && moved <= CLICK_TOLERANCE)
            clicked = true;
        touchActive = false;
    }

    static inline bool consumeClick()
    {
        if (clicked)
        {
            clicked = false;
            return true;
        }
        return false;
    }

    void handleTouch()
    {
        Screen::TouchPos pos = Screen::getTouchPos();
        int absX = pos.x, absY = pos.y;
        Serial.printf("[Browser] Touch pos x=%d y=%d pressed=%d\n", absX, absY, pos.clicked ? 1 : 0);
        bool inViewport = (absX >= vpX && absX < vpX + vpW && absY >= vpY && absY < vpY + vpH && viewportActive);
        int localX = inViewport ? (absX - vpX) : absX;
        int localY = inViewport ? (absY - vpY) : absY;

        if (pos.clicked)
        {
            if (!touchActive)
            {
                onTouchStart(absX, absY);
                return;
            }
            else
            {
                int16_t prevLastY = lastY;
                onTouchMove(absX, absY);
                int listOriginY = viewportActive ? vpY : VISIT_LIST_Y;
                if (dragging && startY >= listOriginY && inViewport && loc.state == "home")
                {
                    int dy = prevLastY - lastY;
                    if (dy != 0)
                    {
                        std::vector<String> sites = LocalStorage::listSites();
                        clampScrollForSites(sites);
                        int totalHeight = (int)sites.size() * VISIT_ITEM_H;
                        int visible = vpH;
                        int maxOffset = totalHeight > visible ? totalHeight - visible : 0;
                        visitScrollOffset += dy;
                        if (visitScrollOffset < 0)
                            visitScrollOffset = 0;
                        if (visitScrollOffset > maxOffset)
                            visitScrollOffset = maxOffset;
                        ReRender();
                    }
                }
                return;
            }
        }
        else
        {
            if (touchActive)
            {
                onTouchEnd();
                if (consumeClick())
                {
                    int clickX = startX, clickY = startY;
                    bool clickInViewport = (clickX >= vpX && clickX < vpX + vpW && clickY >= vpY && clickY < vpY + vpH && viewportActive);
                    int clickLocalX = clickInViewport ? (clickX - vpX) : clickX;
                    int clickLocalY = clickInViewport ? (clickY - vpY) : clickY;

                    if (clickY < TOPBAR_H && clickX > (SCREEN_W - 60))
                    {
                        loc.state = "home";
                        ReRender();
                        return;
                    }

                    if (clickY < TOPBAR_H && clickX < 120)
                    {
                        loc.state = "home";
                        ReRender();
                        return;
                    }

                    if (loc.state == "home")
                    {
                        int cardX = 6, cardY = 25, cardW = SCREEN_W - cardX * 2;
                        int btnW = (cardW - BUTTON_PADDING * 3) / 2;
                        int bx0 = cardX + BUTTON_PADDING;
                        int by = cardY + 6;
                        if (clickY >= by && clickY <= by + BUTTON_H)
                        {
                            for (int i = 0; i < 2; ++i)
                            {
                                int bx = bx0 + i * (btnW + BUTTON_PADDING);
                                if (clickX >= bx && clickX <= bx + btnW)
                                {
                                    if (i == 0)
                                    {
                                        String input = promptText("Which page do you want to visit?", "");
                                        if (input.length())
                                        {
                                            int at = input.indexOf('@');
                                            String domainPort = input;
                                            String state = "startpage";
                                            if (at > 0)
                                            {
                                                domainPort = input.substring(0, at);
                                                state = input.substring(at + 1);
                                            }
                                            int colon = domainPort.indexOf(':');
                                            String domain = domainPort;
                                            int port = 443;
                                            if (colon > 0)
                                            {
                                                domain = domainPort.substring(0, colon);
                                                port = domainPort.substring(colon + 1).toInt();
                                            }
                                            navigate(domain, port, state);
                                        }
                                    }
                                    else
                                    {
                                        navigate("mw-search-server-onrender-app.onrender.com", 443, "startpage");
                                    }
                                    return;
                                }
                            }
                        }
                    }

                    if (loc.state == "home" && clickInViewport)
                    {
                        std::vector<String> sites = LocalStorage::listSites();
                        if (!sites.empty())
                        {
                            int idx = (clickLocalY + visitScrollOffset) / VISIT_ITEM_H;
                            if (idx >= 0 && idx < (int)sites.size())
                            {
                                String domain = sites[idx];
                                int btnW = 56;
                                int btnGap = 4;
                                int xOpen = vpW - BUTTON_PADDING - btnW;
                                int xClear = xOpen - btnGap - btnW;
                                int xDelete = xClear - btnGap - btnW;
                                if (clickLocalX >= xDelete && clickLocalX <= xDelete + btnW)
                                {
                                    LocalStorage::del(domain);
                                    ReRender();
                                    return;
                                }
                                if (clickLocalX >= xClear && clickLocalX <= xClear + btnW)
                                {
                                    LocalStorage::set(domain, String());
                                    ReRender();
                                    return;
                                }
                                if (clickLocalX >= xOpen && clickLocalX <= xOpen + btnW)
                                {
                                    navigate(domain, 443, "startpage");
                                    return;
                                }
                                int textAreaW = vpW - BUTTON_PADDING - (3 * (btnW + btnGap));
                                if (clickLocalX >= 0 && clickLocalX <= textAreaW)
                                {
                                    navigate(domain, 443, "startpage");
                                    return;
                                }
                            }
                        }
                    }
                    else if (loc.state == "home" && clickY >= VISIT_LIST_Y)
                    {
                        std::vector<String> sites = LocalStorage::listSites();
                        int idx = (clickY - VISIT_LIST_Y + visitScrollOffset) / VISIT_ITEM_H;
                        if (idx >= 0 && idx < (int)sites.size())
                        {
                            String domain = sites[idx];
                            int btnW = 56;
                            int btnGap = 4;
                            int xOpen = SCREEN_W - BUTTON_PADDING - btnW;
                            int xClear = xOpen - btnGap - btnW;
                            int xDelete = xClear - btnGap - btnW;
                            if (clickX >= xDelete && clickX <= xDelete + btnW)
                            {
                                LocalStorage::del(domain);
                                ReRender();
                                return;
                            }
                            if (clickX >= xClear && clickX <= xClear + btnW)
                            {
                                LocalStorage::set(domain, String());
                                ReRender();
                                return;
                            }
                            if (clickX >= xOpen && clickX <= xOpen + btnW)
                            {
                                navigate(domain, 443, "startpage");
                                return;
                            }
                            int textAreaW = SCREEN_W - BUTTON_PADDING - (3 * (btnW + btnGap));
                            if (clickX >= 0 && clickX <= textAreaW)
                            {
                                navigate(domain, 443, "startpage");
                                return;
                            }
                        }
                    }

                    if (loc.state != "home" && clickY < TOPBAR_H && clickX > (SCREEN_W - 40))
                    {
                        loc.state = "home";
                        ReRender();
                        return;
                    }
                }
            }
        }
    }

    // ---------------------------
    // Update loop: must be called frequently from main loop
    // ---------------------------
    void Update()
    {
        webSocket.loop();
        handleTouch();
    }

    // ---------------------------
    // Extra: clear stored settings
    // ---------------------------
    void clearSettings()
    {
        loc.session = "";
        loc.state = "home";
        LocalStorage::clearAll();
        Serial.println("[Browser] clearSettings");
    }

    uint16_t getThemeColor(const String &name)
    {
        if (name == "bg")
            return theme.bg;
        if (name == "text")
            return theme.text;
        if (name == "primary")
            return theme.primary;
        if (name == "accent")
            return theme.accent;
        if (name == "accent2")
            return theme.accent2;
        if (name == "accentText")
            return theme.accentText;
        if (name == "pressed")
            return theme.pressed;
        if (name == "danger")
            return theme.danger;
        if (name == "placeholder")
            return theme.placeholder;
        return TFT_WHITE;
    }

    // store/load raw data helpers for external use
    void storeData(const String &domain, const String &data)
    {
        LocalStorage::set(domain, data);
    }
    String loadData(const String &domain)
    {
        return LocalStorage::get(domain);
    }

} // namespace Browser
