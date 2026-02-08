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

    // Scrolling / touch state
    static int visitScrollOffset = 0;
    static bool touchDragging = false;
    static int touchStartY = 0;
    static int touchLastY = 0;
    static int touchTotalMove = 0;

    // Viewport state
    static bool viewportActive = false;
    static int vpX = 0, vpY = 0, vpW = SCREEN_W, vpH = SCREEN_H;

    // Forward declarations (internal)
    void renderTopBar();
    void showHomeUI();
    void showVisitedSites();
    void showWebsitePage();
    void ReRender();

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
    }

    void exitViewport()
    {
        viewportActive = false;
        vpX = 0;
        vpY = 0;
        vpW = SCREEN_W;
        vpH = SCREEN_H;
        Screen::tft.setViewport(0, 0, SCREEN_W, SCREEN_H);
    }

    // ----------------------------
    // Lifecycle
    // ----------------------------
    void Start()
    {
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
                    Serial.println("[Browser] Connected");
                    webSocket.sendTXT("MWOSP-v1 " + Location::sessionId + " " + String(SCREEN_W) + " " + String(SCREEN_H));
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
                    break;
                }
                case WStype_TEXT:
                    handleCommand(msg);
                    break;
                case WStype_DISCONNECTED:
                    Serial.println("[Browser] Disconnected");
                    break;
                default:
                    break;
            } });

        isRunning = true;
    }

    void OnExit()
    {
        webSocket.disconnect();
    }

    void Exit()
    {
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
            // single-line format string — reads all remaining chars until newline
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
            return;
        }

        if (payload.startsWith("Navigate"))
        {
            // support: "Navigate home" or "Navigate domain[:port]@state"
            String arg = payload.substring(9);
            arg.trim();
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
            String input = promptText("Enter text:");
            webSocket.sendTXT("GetBackText " + rid + " " + input);
            return;
        }

        if (payload.startsWith("Title "))
        {
            loc.title = payload.substring(6);
            ReRender();
            return;
        }

        // unhandled messages are ignored
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
    }

    String promptText(const String &question, const String &defaultValue)
    {
        return readString(question, defaultValue);
    }

    void clearSettings()
    {
        loc.session = "";
        loc.state = "home";
        ENC_FS::BrowserStorage::clearAll();
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
    }
    ENC_FS::Buffer loadData(const String &domain)
    {
        return ENC_FS::BrowserStorage::get(domain);
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
    // Touch & click handling
    // ----------------------------
    void handleTouch()
    {
        Screen::TouchPos pos = Screen::getTouchPos();
        int absX = pos.x;
        int absY = pos.y;

        // Determine if pointer is inside our list viewport (when list rendered we call enterViewport)
        bool inViewport = (absX >= vpX && absX < vpX + vpW && absY >= vpY && absY < vpY + vpH && viewportActive);
        int localX = inViewport ? (absX - vpX) : absX;
        int localY = inViewport ? (absY - vpY) : absY;

        if (pos.clicked)
        {
            if (!touchDragging)
            {
                touchDragging = true;
                touchStartY = absY;
                touchLastY = absY;
                touchTotalMove = 0;
                return;
            }

            int dy = touchLastY - absY; // positive -> user swiped up
            if (dy != 0)
            {
                touchTotalMove += abs(dy);
                touchLastY = absY;

                int listOriginY = viewportActive ? vpY : VISIT_LIST_Y;
                if (touchStartY >= listOriginY && inViewport && loc.state == "home")
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
                    ReRender();
                    return;
                }
            }
            return;
        }
        else
        {
            if (touchDragging)
            {
                int moved = touchTotalMove;
                touchDragging = false;
                if (moved > 6)
                    return; // it was a drag, ignore as click
                // else fall through to treat as click
            }
        }

        // simple taps / clicks
        if (!pos.clicked)
        {
            if (!pos.clicked)
                return;

            // Topbar Exit (right)
            if (absY < TOPBAR_H && absX > (SCREEN_W - 60))
            {
                // For production: Exit should close app or disconnect; here go to home
                loc.state = "home";
                ReRender();
                return;
            }

            // Top-left home area -> go home
            if (absY < TOPBAR_H && absX < 120)
            {
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
                if (absY >= by && absY <= by + BUTTON_H)
                {
                    for (int i = 0; i < 2; ++i)
                    {
                        int bx = bx0 + i * (btnW + BUTTON_PADDING);
                        if (absX >= bx && absX <= bx + btnW)
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
                            else // search
                            {
                                navigate("mw-search-server-onrender-app.onrender.com", 443, "startpage");
                            }
                            return;
                        }
                    }
                }
            }

            // Visited list hit-testing — use viewport-relative coords when active
            if (loc.state == "home" && inViewport)
            {
                std::vector<String> sites = ENC_FS::BrowserStorage::listSites();
                if (!sites.empty())
                {
                    int idx = (localY + visitScrollOffset) / VISIT_ITEM_H;
                    if (idx >= 0 && idx < (int)sites.size())
                    {
                        String domain = sites[idx];
                        int btnW = 56;
                        int btnGap = 4;
                        int xOpen = vpW - BUTTON_PADDING - btnW;
                        int xClear = xOpen - btnGap - btnW;
                        int xDelete = xClear - btnGap - btnW;

                        if (localX >= xDelete && localX <= xDelete + btnW)
                        {
                            ENC_FS::BrowserStorage::del(domain);
                            ReRender();
                            return;
                        }
                        if (localX >= xClear && localX <= xClear + btnW)
                        {
                            ENC_FS::Buffer empty;
                            ENC_FS::BrowserStorage::set(domain, empty);
                            ReRender();
                            return;
                        }
                        if (localX >= xOpen && localX <= xOpen + btnW)
                        {
                            navigate(domain, 443, "startpage");
                            return;
                        }

                        int textAreaW = vpW - BUTTON_PADDING - (3 * (btnW + btnGap));
                        if (localX >= 0 && localX <= textAreaW)
                        {
                            navigate(domain, 443, "startpage");
                            return;
                        }
                    }
                }
            }
            else if (loc.state == "home" && absY >= VISIT_LIST_Y)
            {
                // fallback when viewport wasn't used
                std::vector<String> sites = ENC_FS::BrowserStorage::listSites();
                int idx = (absY - VISIT_LIST_Y + visitScrollOffset) / VISIT_ITEM_H;
                if (idx >= 0 && idx < (int)sites.size())
                {
                    String domain = sites[idx];
                    int btnW = 56;
                    int btnGap = 4;
                    int xOpen = SCREEN_W - BUTTON_PADDING - btnW;
                    int xClear = xOpen - btnGap - btnW;
                    int xDelete = xClear - btnGap - btnW;

                    if (absX >= xDelete && absX <= xDelete + btnW)
                    {
                        ENC_FS::BrowserStorage::del(domain);
                        ReRender();
                        return;
                    }
                    if (absX >= xClear && absX <= xClear + btnW)
                    {
                        ENC_FS::Buffer empty;
                        ENC_FS::BrowserStorage::set(domain, empty);
                        ReRender();
                        return;
                    }
                    if (absX >= xOpen && absX <= xOpen + btnW)
                    {
                        navigate(domain, 443, "startpage");
                        return;
                    }

                    int textAreaW = SCREEN_W - BUTTON_PADDING - (3 * (btnW + btnGap));
                    if (absX >= 0 && absX <= textAreaW)
                    {
                        navigate(domain, 443, "startpage");
                        return;
                    }
                }
            }

            // Website topbar exit
            if (loc.state != "home" && absY < TOPBAR_H && absX > (SCREEN_W - 40))
            {
                loc.state = "home";
                ReRender();
                return;
            }
        }
    }

    // ----------------------------
    // Navigation
    // ----------------------------
    void navigate(const String &domain, int port, const String &state)
    {
        loc.domain = domain;
        loc.port = port;
        loc.state = state;
        saveVisitedSite(domain);

        webSocket.disconnect();
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
