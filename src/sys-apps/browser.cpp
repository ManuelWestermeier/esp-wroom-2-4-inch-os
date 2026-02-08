// browser.cpp — Production-ready two-page browser (home, website)
// Matches browser.hpp API. Focus: robust viewport, visited-list scrolling, hit-testing,
// safe parsing of incoming Draw* commands, no out-of-bounds rendering.

#include "browser.hpp"

#include <WiFiClientSecure.h>
#include <WebSocketsClient.h>

#include "../screen/index.hpp"
#include "../io/read-string.hpp"
#include "../styles/global.hpp"
#include "../fs/enc-fs.hpp"
#include "nanosvg.h"

namespace Browser
{
    // Ensure these match the declaration in browser.hpp
    Location loc;
    bool isRunning = false;
    WebSocketsClient webSocket;
    String Location::sessionId = String(random(100000, 999999));

    // Recent input cache (keeps last prompt quickly accessible)
    static std::vector<String> recentInputs;

    // UI layout constants (320x240)
    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 240;
    static constexpr int TOPBAR_H = 20;
    static constexpr int BUTTON_PADDING = 10;
    static constexpr int BUTTON_H = 36;
    static constexpr int VISIT_LIST_Y = 100;
    static constexpr int VISIT_ITEM_H = 30;
    static constexpr int VIEWPORT_Y = TOPBAR_H;
    static constexpr int VIEWPORT_H = SCREEN_H - TOPBAR_H;

    // Touch / click tuning
    static constexpr uint16_t CLICK_TOLERANCE = 12; // pixels (Manhattan)
    static constexpr uint16_t STABILIZE_MS = 30;    // ms to ignore initial jitter

    // Theme (16-bit values stored in uint16_t)
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

    // Scrolling / touch state (reworked)
    static int visitScrollOffset = 0;

    static bool touchActive = false;
    static bool dragging = false;
    static bool clicked = false;

    static int16_t startX = 0;
    static int16_t startY = 0;
    static int16_t lastX = 0;
    static int16_t lastY = 0;

    static uint32_t touchStartMs = 0;
    static uint16_t moved = 0;

    // Viewport state
    static bool viewportActive = false;
    static int vpX = 0, vpY = 0, vpW = SCREEN_W, vpH = SCREEN_H;

    // Forward declarations (internal)
    void renderTopBar();
    void showHomeUI();
    void showVisitedSites();
    void showWebsitePage();
    void ReRender();

    // touch helpers
    static inline void onTouchStart(int16_t x, int16_t y)
    {
        touchActive = true;
        dragging = false;
        clicked = false;
        startX = lastX = x;
        startY = lastY = y;
        moved = 0;
        touchStartMs = millis();
        Serial.printf("[Browser] onTouchStart x=%d y=%d\n", x, y);
    }

    static inline void onTouchMove(int16_t x, int16_t y)
    {
        if (!touchActive)
            return;

        // ignore unstable readings at the beginning
        if (millis() - touchStartMs < STABILIZE_MS)
        {
            lastX = x;
            lastY = y;
            return;
        }

        // Manhattan distance from start (stable)
        moved = abs(x - startX) + abs(y - startY);

        if (moved > CLICK_TOLERANCE)
            dragging = true;

        // update last coordinates (used by scrolling calculation outside)
        lastX = x;
        lastY = y;
    }

    static inline void onTouchEnd()
    {
        if (!touchActive)
            return;

        // final distance check
        if (!dragging && moved <= CLICK_TOLERANCE)
            clicked = true;

        Serial.printf("[Browser] onTouchEnd moved=%d dragging=%d clicked=%d\n", moved, dragging ? 1 : 0, clicked ? 1 : 0);

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

    // ----------------------------
    // Viewport helpers
    // ----------------------------
    void enterViewport(int x, int y, int w, int h)
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

    void exitViewport()
    {
        viewportActive = false;
        vpX = 0;
        vpY = 0;
        vpW = SCREEN_W;
        vpH = SCREEN_H;
        Screen::tft.setViewport(0, 0, SCREEN_W, SCREEN_H);

        Serial.println("[Browser] exitViewport");
    }

    // ----------------------------
    // Lifecycle
    // ----------------------------
    void Start()
    {
        Serial.println("[Browser] Start");
        Screen::tft.fillScreen(theme.bg);
        renderTopBar();
        showHomeUI();
        showVisitedSites();

        webSocket.onEvent([](WStype_t type, uint8_t *payload, size_t length)
                          {
            String msg = (payload != nullptr && length > 0) ? String((char *)payload) : String();

            switch (type)
            {
                case WStype_CONNECTED:
                {
                    Serial.println("[Browser] WebSocket Connected");
                    // prefer loc.session if present (use context correctly)
                    String sess = (loc.session.length() ? loc.session : Location::sessionId);
                    Serial.printf("[Browser] Using session id: %s\n", sess.c_str());
                    webSocket.sendTXT("MWOSP-v1 " + sess + " " + String(SCREEN_W) + " " + String(SCREEN_H));
                    String themeMsg = "ThemeColors";
                    themeMsg += " bg:" + String(theme.bg, HEX);
                    themeMsg += " text:" + String(theme.text, HEX);
                    themeMsg += " primary:" + String(theme.primary, HEX);
                    themeMsg += " accent:" + String(theme.accent, HEX);
                    themeMsg += " accent2:" + String(theme.accent2, HEX);
                    themeMsg += " accentText:" + String(theme.accentText, HEX);
                    themeMsg += " pressed:" + String(theme.pressed, HEX);
                    themeMsg += " danger:" + String(theme.danger, HEX);
                    themeMsg += " placeholder:" + String(theme.placeholder, HEX);
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
            } });

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

    // ----------------------------
    // Safe small helpers
    // ----------------------------
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

    // ----------------------------
    // Command handling (server -> device)
    // ----------------------------
    void handleCommand(const String &payload)
    {
        Serial.printf("[Browser] handleCommand payload='%s'\n", payload.c_str());

        if (payload.startsWith("FillRect"))
        {
            int x = 0, y = 0, w = 0, h = 0;
            unsigned int c = 0;
            if (sscanf(payload.c_str(), "FillRect %d %d %d %d %u", &x, &y, &w, &h, &c) == 5)
            {
                Serial.printf("[Browser] FillRect parsed x=%d y=%d w=%d h=%d color=0x%X\n", x, y, w, h, c);
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
                Serial.printf("[Browser] DrawCircle x=%d y=%d r=%d color=0x%X\n", x, y, r, c);
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
            // single-line format string — reads all remaining chars until newline
            if (sscanf(payload.c_str(), "DrawText %d %d %d %u %[^\n]", &x, &y, &size, &c, buf) >= 5)
            {
                String text = String(buf);
                Serial.printf("[Browser] DrawText x=%d y=%d size=%d color=0x%X text='%s'\n", x, y, size, c, text.c_str());
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
                Serial.printf("[Browser] DrawSVG x=%d y=%d w=%d h=%d color=0x%X\n", x, y, w, h, c);
                // find payload after 5th token
                int countSpaces = 0;
                int headerEnd = -1;
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

        // Theme / storage / navigation commands — lightweight parsing
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
                Serial.printf("[Browser] SetThemeColor %s -> 0x%X\n", colorName.c_str(), color);
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
            uint16_t c = getThemeColor(name);
            webSocket.sendTXT("ThemeColor " + name + " " + String(c, HEX));
            Serial.printf("[Browser] GetThemeColor %s -> 0x%X\n", name.c_str(), c);
            return;
        }

        if (payload.startsWith("SetStorage"))
        {
            int idx = payload.indexOf(' ', 11);
            if (idx > 0)
            {
                String key = payload.substring(11, idx);
                String val = payload.substring(idx + 1);
                ENC_FS::Buffer buf((uint8_t *)val.c_str(), (uint8_t *)val.c_str() + val.length());
                ENC_FS::BrowserStorage::set(key, buf);
                Serial.printf("[Browser] SetStorage key='%s' len=%d\n", key.c_str(), (int)val.length());
            }
            return;
        }

        if (payload.startsWith("GetStorage"))
        {
            String key = payload.substring(11);
            ENC_FS::Buffer b = ENC_FS::BrowserStorage::get(key);
            String s;
            if (!b.empty())
                s = String((char *)b.data(), b.size());
            webSocket.sendTXT("GetBackStorage " + key + " " + s);
            Serial.printf("[Browser] GetStorage key='%s' len=%d\n", key.c_str(), (int)s.length());
            return;
        }

        if (payload.startsWith("Navigate"))
        {
            // support: "Navigate home" or "Navigate domain[:port]@state"
            String arg = payload.substring(9);
            arg.trim();
            Serial.printf("[Browser] Navigate arg='%s'\n", arg.c_str());
            if (arg == "home")
            {
                loc.state = "home";
                ReRender();
                return;
            }
            // parse domain[:port]@state
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
            Serial.println("[Browser] Received Exit command");
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
            Serial.printf("[Browser] PromptText rid='%s'\n", rid.c_str());
            // Ensure UI is up-to-date before prompting
            ReRender();
            String input = promptText("Enter text:", "");
            // cache last input to ENC_FS quickly for retrieval
            ENC_FS::Buffer cacheBuf((uint8_t *)input.c_str(), (uint8_t *)input.c_str() + input.length());
            ENC_FS::BrowserStorage::set("_last_input", cacheBuf);
            if (input.length())
            {
                // store also into in-memory recentInputs (bounded)
                recentInputs.insert(recentInputs.begin(), input);
                if (recentInputs.size() > 8)
                    recentInputs.pop_back();
            }
            Serial.printf("[Browser] PromptText input='%s'\n", input.c_str());
            webSocket.sendTXT("GetBackText " + rid + " " + input);
            // After prompt, refresh UI quickly
            ReRender();
            return;
        }

        if (payload.startsWith("Title "))
        {
            loc.title = payload.substring(6);
            Serial.printf("[Browser] Title set to '%s'\n", loc.title.c_str());
            ReRender();
            return;
        }

        // unhandled messages are ignored
        Serial.println("[Browser] Unhandled command");
    }

    // ----------------------------
    // Main loop
    // ----------------------------
    void Update()
    {
        webSocket.loop();
        handleTouch();
    }

    // ----------------------------
    // Full redraw
    // ----------------------------
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
        else // only website page beyond home
        {
            showWebsitePage();
        }
    }

    // ----------------------------
    // Drawing helpers (viewport-aware clipping & truncation)
    // ----------------------------
    void drawText(int x, int y, const String &text, uint16_t color, int size)
    {
        // Determine local width/height depending on viewportActive
        int localW = viewportActive ? vpW : SCREEN_W;
        int localH = viewportActive ? vpH : SCREEN_H;

        if (y < 0 || y >= localH)
            return;
        Screen::tft.setTextSize(size);
        Screen::tft.setTextColor(color);
        Screen::tft.setCursor(x, y);

        // approximate char width = 6 * size
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
        // drawCircle uses absolute coords
        Screen::tft.drawCircle(x, y, r, color);
    }

    void drawSVG(const String &svgStr, int x, int y, int w, int h, uint16_t color)
    {
        NSVGimage *img = createSVG(svgStr);
        if (img)
            drawSVGString(svgStr, x, y, w, h, color);
        else
            Serial.println("[Browser] drawSVG: failed to parse SVG");
    }

    String promptText(const String &question, const String &defaultValue)
    {
        // Refresh UI before prompting to make response feel instant
        ReRender();
        String res = readString(question, defaultValue);
        // quick in-memory cache: push front, keep small
        if (res.length())
        {
            recentInputs.insert(recentInputs.begin(), res);
            if (recentInputs.size() > 8)
                recentInputs.pop_back();
        }
        Serial.printf("[Browser] promptText question='%s' -> '%s'\n", question.c_str(), res.c_str());
        return res;
    }

    void clearSettings()
    {
        loc.session = "";
        loc.state = "home";
        ENC_FS::BrowserStorage::clearAll();
        Serial.println("[Browser] clearSettings called - storage cleared");
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

    void storeData(const String &domain, const ENC_FS::Buffer &data)
    {
        ENC_FS::BrowserStorage::set(domain, data);
        Serial.printf("[Browser] storeData domain='%s' len=%d\n", domain.c_str(), (int)data.size());
    }
    ENC_FS::Buffer loadData(const String &domain)
    {
        ENC_FS::Buffer b = ENC_FS::BrowserStorage::get(domain);
        Serial.printf("[Browser] loadData domain='%s' len=%d\n", domain.c_str(), (int)b.size());
        return b;
    }

    // ----------------------------
    // Top bar
    // ----------------------------
    void renderTopBar()
    {
        Screen::tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, theme.primary);
        drawText(6, 3, "Title Home", theme.text, 2);
        drawText(SCREEN_W - 48, 3, "Exit", theme.danger, 2);
    }

    // ----------------------------
    // Touch & click handling (REPLACED)
    // ----------------------------
    void handleTouch()
    {
        Screen::TouchPos pos = Screen::getTouchPos();
        int absX = pos.x;
        int absY = pos.y;

        Serial.printf("[Browser] Touch pos x=%d y=%d pressed=%d\n", absX, absY, pos.clicked ? 1 : 0);

        // Determine if pointer is inside our list viewport (when list rendered we call enterViewport)
        bool inViewport = (absX >= vpX && absX < vpX + vpW && absY >= vpY && absY < vpY + vpH && viewportActive);
        int localX = inViewport ? (absX - vpX) : absX;
        int localY = inViewport ? (absY - vpY) : absY;

        // pressed
        if (pos.clicked)
        {
            if (!touchActive)
            {
                // start new touch
                onTouchStart(absX, absY);
                return;
            }
            else
            {
                // move
                int16_t prevLastY = lastY;
                onTouchMove(absX, absY);
                // if we are dragging and started inside the list area, scroll
                int listOriginY = viewportActive ? vpY : VISIT_LIST_Y;
                if (dragging && startY >= listOriginY && inViewport && loc.state == "home")
                {
                    int dy = prevLastY - lastY; // positive -> user swiped up
                    if (dy != 0)
                    {
                        std::vector<String> sites = ENC_FS::BrowserStorage::listSites();
                        clampScrollForSites(sites);
                        int totalHeight = (int)sites.size() * VISIT_ITEM_H;
                        int visible = vpH;
                        int maxOffset = totalHeight > visible ? totalHeight - visible : 0;
                        visitScrollOffset += dy;
                        if (visitScrollOffset < 0)
                            visitScrollOffset = 0;
                        if (visitScrollOffset > maxOffset)
                            visitScrollOffset = maxOffset;
                        Serial.printf("[Browser] Dragging list dy=%d visitScrollOffset=%d\n", dy, visitScrollOffset);
                        ReRender();
                    }
                }
                return;
            }
        }
        else
        {
            // released
            if (touchActive)
            {
                onTouchEnd();
                // if it was a click, handle it using start coords (stabilized) -> map to local coords
                if (consumeClick())
                {
                    int clickX = startX;
                    int clickY = startY;
                    bool clickInViewport = (clickX >= vpX && clickX < vpX + vpW && clickY >= vpY && clickY < vpY + vpH && viewportActive);
                    int clickLocalX = clickInViewport ? (clickX - vpX) : clickX;
                    int clickLocalY = clickInViewport ? (clickY - vpY) : clickY;

                    Serial.printf("[Browser] Click detected x=%d y=%d (localX=%d localY=%d) inViewport=%d state=%s\n",
                                  clickX, clickY, clickLocalX, clickLocalY, clickInViewport ? 1 : 0, loc.state.c_str());

                    // Topbar Exit (right)
                    if (clickY < TOPBAR_H && clickX > (SCREEN_W - 60))
                    {
                        Serial.println("[Browser] Topbar Exit tapped");
                        loc.state = "home";
                        ReRender();
                        return;
                    }

                    // Top-left home area -> go home
                    if (clickY < TOPBAR_H && clickX < 120)
                    {
                        Serial.println("[Browser] Topbar Home tapped");
                        loc.state = "home";
                        ReRender();
                        return;
                    }

                    // Home buttons
                    if (loc.state == "home")
                    {
                        int cardX = 6;
                        int cardY = 25;
                        int cardW = SCREEN_W - cardX * 2;
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
                                        Serial.println("[Browser] Open Site button tapped");
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
                                    else // search
                                    {
                                        Serial.println("[Browser] Open Search button tapped");
                                        navigate("mw-search-server-onrender-app.onrender.com", 443, "startpage");
                                    }
                                    return;
                                }
                            }
                        }
                    }

                    // Visited list hit-testing — use viewport-relative coords when active
                    if (loc.state == "home" && clickInViewport)
                    {
                        std::vector<String> sites = ENC_FS::BrowserStorage::listSites();
                        if (!sites.empty())
                        {
                            int idx = (clickLocalY + visitScrollOffset) / VISIT_ITEM_H;
                            Serial.printf("[Browser] Click in viewport idx=%d localX=%d localY=%d\n", idx, clickLocalX, clickLocalY);
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
                                    Serial.printf("[Browser] Delete site '%s'\n", domain.c_str());
                                    ENC_FS::BrowserStorage::del(domain);
                                    ReRender();
                                    return;
                                }
                                if (clickLocalX >= xClear && clickLocalX <= xClear + btnW)
                                {
                                    Serial.printf("[Browser] Clear site content '%s'\n", domain.c_str());
                                    ENC_FS::Buffer empty;
                                    ENC_FS::BrowserStorage::set(domain, empty);
                                    ReRender();
                                    return;
                                }
                                if (clickLocalX >= xOpen && clickLocalX <= xOpen + btnW)
                                {
                                    Serial.printf("[Browser] Open site '%s'\n", domain.c_str());
                                    navigate(domain, 443, "startpage");
                                    return;
                                }

                                int textAreaW = vpW - BUTTON_PADDING - (3 * (btnW + btnGap));
                                if (clickLocalX >= 0 && clickLocalX <= textAreaW)
                                {
                                    Serial.printf("[Browser] Text area tapped - open '%s'\n", domain.c_str());
                                    navigate(domain, 443, "startpage");
                                    return;
                                }
                            }
                        }
                    }
                    else if (loc.state == "home" && clickY >= VISIT_LIST_Y)
                    {
                        // fallback when viewport wasn't used
                        std::vector<String> sites = ENC_FS::BrowserStorage::listSites();
                        int idx = (clickY - VISIT_LIST_Y + visitScrollOffset) / VISIT_ITEM_H;
                        Serial.printf("[Browser] Click in fallback list idx=%d absX=%d absY=%d\n", idx, clickX, clickY);
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
                                Serial.printf("[Browser] Delete site '%s' (fallback)\n", domain.c_str());
                                ENC_FS::BrowserStorage::del(domain);
                                ReRender();
                                return;
                            }
                            if (clickX >= xClear && clickX <= xClear + btnW)
                            {
                                Serial.printf("[Browser] Clear site '%s' (fallback)\n", domain.c_str());
                                ENC_FS::Buffer empty;
                                ENC_FS::BrowserStorage::set(domain, empty);
                                ReRender();
                                return;
                            }
                            if (clickX >= xOpen && clickX <= xOpen + btnW)
                            {
                                Serial.printf("[Browser] Open site '%s' (fallback)\n", domain.c_str());
                                navigate(domain, 443, "startpage");
                                return;
                            }

                            int textAreaW = SCREEN_W - BUTTON_PADDING - (3 * (btnW + btnGap));
                            if (clickX >= 0 && clickX <= textAreaW)
                            {
                                Serial.printf("[Browser] Text area tapped - open '%s' (fallback)\n", domain.c_str());
                                navigate(domain, 443, "startpage");
                                return;
                            }
                        }
                    }

                    // Website topbar exit
                    if (loc.state != "home" && clickY < TOPBAR_H && clickX > (SCREEN_W - 40))
                    {
                        Serial.println("[Browser] Website topbar close tapped");
                        loc.state = "home";
                        ReRender();
                        return;
                    }
                }
            }
        }
    }

    // ----------------------------
    // Navigation
    // ----------------------------
    void navigate(const String &domain, int port, const String &state)
    {
        Serial.printf("[Browser] navigate domain='%s' port=%d state='%s'\n", domain.c_str(), port, state.c_str());
        loc.domain = domain;
        loc.port = port;
        loc.state = state;
        saveVisitedSite(domain);

        webSocket.disconnect();
        // small delay can help WebSockets reconnect reliably on some hardware; keep minimal
        delay(10);
        webSocket.beginSSL(loc.domain.c_str(), loc.port, "/");
        webSocket.setReconnectInterval(5000);
        ReRender();
    }

    void saveVisitedSite(const String &domain)
    {
        unsigned long ts = millis();
        String s = String(ts);
        ENC_FS::Buffer b((uint8_t *)s.c_str(), (uint8_t *)s.c_str() + s.length());
        ENC_FS::BrowserStorage::set(domain, b);
        Serial.printf("[Browser] saveVisitedSite domain='%s' ts=%lu\n", domain.c_str(), ts);
    }

    // ----------------------------
    // UI: Home & Website
    // ----------------------------
    void showHomeUI()
    {
        int cardX = 6;
        int cardY = 25;
        int cardW = SCREEN_W - cardX * 2;
        int cardH = 48;

        Screen::tft.fillRoundRect(cardX, cardY, cardW, cardH, 6, theme.primary);
        Screen::tft.fillRoundRect(cardX + 2, cardY + 2, cardW - 4, cardH - 4, 6, theme.bg);

        int btnW = (cardW - BUTTON_PADDING * 3) / 2;
        int bx = cardX + BUTTON_PADDING;
        int by = cardY + 6;

        Screen::tft.fillRoundRect(bx, by, btnW, BUTTON_H, 6, theme.accent);
        drawText(bx + 8, by + 8, "Open Site", theme.accentText, 2);

        bx += btnW + BUTTON_PADDING;
        Screen::tft.fillRoundRect(bx, by, btnW, BUTTON_H, 6, theme.accent2);
        drawText(bx + 8, by + 8, "Open Search", theme.accentText, 2);
    }

    void showVisitedSites()
    {
        std::vector<String> sites = ENC_FS::BrowserStorage::listSites();
        Serial.printf("[Browser] showVisitedSites count=%d visitScrollOffset=%d\n", (int)sites.size(), visitScrollOffset);
        drawText(10, VISIT_LIST_Y - 18, "Visited Sites", theme.text, 2);

        // Use viewport for list area
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
        Serial.printf("[Browser] showWebsitePage title='%s' domain='%s'\n", loc.title.c_str(), loc.domain.c_str());
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

} // namespace Browser
