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

    // UI layout constants
    static constexpr int SCREEN_W = 320;
    static constexpr int SCREEN_H = 240;
    static constexpr int TOPBAR_H = 20;               // task-bar 20px high
    static constexpr int BUTTON_ROW_Y = TOPBAR_H + 6; // card y
    static constexpr int BUTTON_H = 36;
    static constexpr int BUTTON_PADDING = 10;
    static constexpr int VISIT_LIST_Y = 100; // under the buttons
    static constexpr int VISIT_ITEM_H = 30;
    static constexpr int VIEWPORT_Y = TOPBAR_H;
    static constexpr int VIEWPORT_H = SCREEN_H - TOPBAR_H;

    // Theme colors storage (can be updated from frontend)
    static struct Theme
    {
        uint16_t bg = Style::Colors::bg;
        uint16_t text = Style::Colors::text;
        uint16_t primary = Style::Colors::primary;
        uint16_t accent = Style::Colors::accent;
        uint16_t accent2 = Style::Colors::accent2;
        uint16_t accent3 = Style::Colors::accent3;
        uint16_t accentText = Style::Colors::accentText;
        uint16_t pressed = Style::Colors::pressed;
        uint16_t danger = Style::Colors::danger;
        uint16_t placeholder = Style::Colors::placeholder;
    } theme;

    // Scroll state for visited list
    static int visitScrollOffset = 0;
    static bool touchDragging = false;
    static int touchStartY = 0;
    static int touchLastY = 0;
    static int touchTotalMove = 0;

    // Viewport tracking (keeps track of current viewport so drawing helpers can clip)
    static bool viewportActive = false;
    static int vpX = 0, vpY = 0, vpW = SCREEN_W, vpH = SCREEN_H;

    // Forward declarations
    void renderTopBar();
    void showHomeUI();
    void showVisitedSites();
    void showWebsitePage();
    void ReRender();

    // Viewport helpers
    void enterViewport(int x, int y, int w, int h)
    {
        // clamp values
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

    // ---- Start / lifecycle ----
    void Start()
    {
        // Render initial UI but do NOT connect the websocket automatically.
        // The remote website connection will be opened only after the user explicitly navigates.
        Screen::tft.fillScreen(theme.bg);
        renderTopBar();
        showHomeUI();
        showVisitedSites();

        // Prepare websocket event handler (connection will be started on navigate())
        webSocket.onEvent([](WStype_t type, uint8_t *payload, size_t length)
                          {
            String msg = (payload != nullptr && length > 0) ? String((char *)payload) : String();

            switch (type)
            {
            case WStype_CONNECTED:
                {
                    Serial.println("[Browser] Connected");
                    // Send handshake with sessionId and device resolution (width x height)
                    webSocket.sendTXT("MWOSP-v1 " + Location::sessionId + " " + String(SCREEN_W) + " " + String(SCREEN_H));
                    // Send current theme colors to server
                    String themeMsg = "ThemeColors";
                    themeMsg += " bg:" + String(theme.bg, HEX);
                    themeMsg += " text:" + String(theme.text, HEX);
                    themeMsg += " primary:" + String(theme.primary, HEX);
                    themeMsg += " accent:" + String(theme.accent, HEX);
                    themeMsg += " accent2:" + String(theme.accent2, HEX);
                    themeMsg += " accent3:" + String(theme.accent3, HEX);
                    themeMsg += " accentText:" + String(theme.accentText, HEX);
                    themeMsg += " pressed:" + String(theme.pressed, HEX);
                    themeMsg += " danger:" + String(theme.danger, HEX);
                    themeMsg += " placeholder:" + String(theme.placeholder, HEX);
                    webSocket.sendTXT(themeMsg);
                }
                break;
            case WStype_TEXT:
                handleCommand(msg);
                break;
            case WStype_DISCONNECTED:
                Serial.println("[Browser] Disconnected");
                break;
            default:
                break;
            } });

        // No automatic .beginSSL() here: connect when navigate() is called.
        isRunning = true;
    }

    void OnExit()
    {
        // Ensure websocket is gracefully closed
        webSocket.disconnect();
    }

    void Exit()
    {
        isRunning = false;
        OnExit();
        // return to home view
        loc.state = "home";
        ReRender();
    }

    // ---- Command handling from server ----
    void handleCommand(const String &payload)
    {
        // ----------------- TFT Drawing with bounds checking -----------------
        if (payload.startsWith("FillRect"))
        {
            int x = 0, y = 0, w = 0, h = 0;
            unsigned int c = 0;
            if (sscanf(payload.c_str(), "FillRect %d %d %d %d %u", &x, &y, &w, &h, &c) == 5)
            {
                // Clip rectangle to screen bounds
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
        }
        else if (payload.startsWith("DrawCircle"))
        {
            int x = 0, y = 0, r = 0;
            unsigned int c = 0;
            if (sscanf(payload.c_str(), "DrawCircle %d %d %d %u", &x, &y, &r, &c) == 4)
            {
                // Clip circle: only draw if any part is visible
                if (r > 0 && x >= -r && x <= SCREEN_W + r && y >= -r && y <= SCREEN_H + r)
                    drawCircle(x, y, r, (uint16_t)c);
            }
        }
        else if (payload.startsWith("DrawText"))
        {
            int x = 0, y = 0, size = 1;
            unsigned int c = 0;
            char buf[512] = {0};
            // Format: DrawText <x> <y> <size> <color> <text...>
            if (sscanf(payload.c_str(), "DrawText %d %d %d %u %[^\n]", &x, &y, &size, &c, buf) >= 5)
            {
                String text = String(buf);
                // Clip text position (use drawText which is viewport aware)
                drawText(x, y, text, (uint16_t)c, size);
            }
        }
        else if (payload.startsWith("DrawSVG"))
        {
            // Format assumed: "DrawSVG x y w h color <svg...>"
            int x = 0, y = 0, w = 0, h = 0;
            unsigned int c = 0;
            int read = sscanf(payload.c_str(), "DrawSVG %d %d %d %d %u", &x, &y, &w, &h, &c);
            if (read == 5)
            {
                // locate 5th space to extract svg payload
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
                    // Clip SVG drawing area
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
        }
        // ----------------- Theme Colors -----------------
        else if (payload.startsWith("SetThemeColor"))
        {
            // Format: "SetThemeColor bg 0xFFFF" (16-bit color in hex)
            int idx = payload.indexOf(' ', 13);
            if (idx > 0)
            {
                String colorName = payload.substring(13, idx);
                String colorValue = payload.substring(idx + 1);

                // Remove "0x" prefix if present
                if (colorValue.startsWith("0x"))
                    colorValue = colorValue.substring(2);

                uint16_t color = (uint16_t)strtoul(colorValue.c_str(), NULL, 16);

                // Update theme color
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
                else if (colorName == "accent3")
                    theme.accent3 = color;
                else if (colorName == "accentText")
                    theme.accentText = color;
                else if (colorName == "pressed")
                    theme.pressed = color;
                else if (colorName == "danger")
                    theme.danger = color;
                else if (colorName == "placeholder")
                    theme.placeholder = color;

                // Redraw UI with new colors if needed
                if (loc.state == "home" || loc.state == "settings")
                {
                    ReRender();
                }
            }
        }
        else if (payload.startsWith("GetThemeColor"))
        {
            // Format: "GetThemeColor bg"
            String colorName = payload.substring(14);
            uint16_t color = getThemeColor(colorName);
            webSocket.sendTXT("ThemeColor " + colorName + " " + String(color, HEX));
        }
        // ----------------- Storage -----------------
        else if (payload.startsWith("GetStorage"))
        {
            // "GetStorage <key>"
            String key = payload.substring(11);
            ENC_FS::Buffer data = ENC_FS::BrowserStorage::get(key);
            String s;
            if (!data.empty())
                s = String((const char *)data.data(), data.size());
            webSocket.sendTXT("GetBackStorage " + key + " " + s);
        }
        else if (payload.startsWith("SetStorage"))
        {
            // "SetStorage <key> <value...>"
            int idx = payload.indexOf(' ', 11);
            if (idx > 0)
            {
                String key = payload.substring(11, idx);
                String val = payload.substring(idx + 1);
                ENC_FS::Buffer buf((uint8_t *)val.c_str(), (uint8_t *)val.c_str() + val.length());
                ENC_FS::BrowserStorage::set(key, buf);
            }
        }
        // ----------------- Navigation & Control -----------------
        else if (payload.startsWith("Navigate"))
        {
            // Format: Navigate <domain>[:port]@<state>
            int idxAt = payload.indexOf('@', 9);
            int idxColon = payload.indexOf(':', 9);
            String domain;
            int port = 443;
            String state = "startpage";

            if (idxAt > 0)
            {
                if (idxColon > 0 && idxColon < idxAt)
                {
                    domain = payload.substring(9, idxColon);
                    port = payload.substring(idxColon + 1, idxAt).toInt();
                }
                else
                    domain = payload.substring(9, idxAt);
                state = payload.substring(idxAt + 1);
            }
            else
            {
                // No @, maybe just "Navigate home" or "Navigate settings"
                domain = payload.substring(9);
                // if domain equals "home" or "settings" treat as state
                if (domain == "home" || domain == "settings" || domain == "search" || domain == "input")
                {
                    loc.state = domain;
                    ReRender();
                    return;
                }
            }
            navigate(domain, port, state);
        }
        else if (payload.startsWith("Exit"))
        {
            Exit();
        }
        else if (payload.startsWith("ClearSettings"))
        {
            clearSettings();
            ReRender();
        }
        else if (payload.startsWith("PromptText"))
        {
            // server asked device for input
            int idx = payload.indexOf(' ', 11);
            String rid;
            if (idx > 0)
                rid = payload.substring(11, idx);
            else
                rid = payload.substring(11);
            String input = promptText("Enter text:");
            webSocket.sendTXT("GetBackText " + rid + " " + input);
        }
        else if (payload.startsWith("SetSession"))
        {
            loc.session = payload.substring(11);
        }
        else if (payload.startsWith("GetSession"))
        {
            String rid = payload.substring(11);
            webSocket.sendTXT("GetBackSession " + rid + " " + loc.session);
        }
        else if (payload.startsWith("SetState"))
        {
            loc.state = payload.substring(9);
        }
        else if (payload.startsWith("GetState"))
        {
            String rid = payload.substring(9);
            webSocket.sendTXT("GetBackState " + rid + " " + loc.state);
        }
        else if (payload.startsWith("Title "))
        {
            // Server sends page title
            loc.title = payload.substring(6);
            ReRender();
        }
    }

    void Update()
    {
        // webSocket.loop is safe even if not connected
        webSocket.loop();
        handleTouch();
    }

    void ReRender()
    {
        // Full redraw according to current loc.state
        Screen::tft.fillScreen(theme.bg);
        renderTopBar();

        if (loc.state == "home" || loc.state == "")
        {
            showHomeUI();
            showVisitedSites();
        }
        else if (loc.state == "settings")
        {
            showSettingsPage();
        }
        else if (loc.state == "startpage" || loc.state == "website")
        {
            showWebsitePage();
        }
        else if (loc.state == "input")
        {
            showInputPage();
        }
        else
        {
            // Fallback to home
            loc.state = "home";
            showHomeUI();
            showVisitedSites();
        }
    }

    // ----------------- Utilities -----------------
    void drawText(int x, int y, const String &text, uint16_t color, int size)
    {
        // If a viewport is active, coordinates are relative to it; otherwise absolute to screen.
        int localW = viewportActive ? vpW : SCREEN_W;
        int localH = viewportActive ? vpH : SCREEN_H;

        // Clip vertical position
        if (y < 0 || y >= localH)
            return;

        Screen::tft.setTextColor(color);
        Screen::tft.setTextSize(size);
        Screen::tft.setCursor(x, y);

        // Truncate text if it would overflow
        String displayText = text;
        int textWidth = text.length() * 6 * size;
        if (x + textWidth > localW)
        {
            int maxChars = (localW - x) / (6 * size);
            if (maxChars > 3)
            {
                displayText = text.substring(0, maxChars - 3) + "...";
            }
            else
            {
                return; // Not enough space
            }
        }

        Screen::tft.print(displayText);
    }

    void drawCircle(int x, int y, int r, uint16_t color)
    {
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
        if (name == "accent3")
            return theme.accent3;
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

    void renderTopBar()
    {
        // top bar background (full width)
        Screen::tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, theme.primary);

        // Left: Home label
        drawText(6, 3, "Home", theme.text, 2);

        // Right: Exit label
        drawText(SCREEN_W - 40, 3, "Exit", theme.danger, 2);
    }

    void handleTouch()
    {
        // manage dragging for the visited list
        Screen::TouchPos pos = Screen::getTouchPos();

        // normalize touch coords and whether inside viewport
        int absX = pos.x;
        int absY = pos.y;
        bool inViewport = (viewportActive && absY >= vpY && absY < (vpY + vpH) && absX >= vpX && absX < (vpX + vpW));
        int localX = inViewport ? (absX - vpX) : absX;
        int localY = inViewport ? (absY - vpY) : absY;

        if (Screen::isTouched())
        {
            if (!touchDragging)
            {
                touchDragging = true;
                touchStartY = absY;
                touchLastY = absY;
                touchTotalMove = 0;
                return; // start of touch, ignore until movement or release
            }

            int dy = touchLastY - absY; // positive when user swipes up
            if (dy != 0)
            {
                touchTotalMove += abs(dy);
                touchLastY = absY;

                // Only apply scroll if touch started inside the list area (use viewport origin if active)
                int listOriginY = viewportActive ? vpY : VISIT_LIST_Y;
                if (touchStartY >= listOriginY && inViewport)
                {
                    std::vector<String> sites = ENC_FS::BrowserStorage::listSites();
                    int totalHeight = (int)sites.size() * VISIT_ITEM_H;
                    int visible = vpH; // viewport height
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
            // Touch released
            if (touchDragging)
            {
                // capture last release position
                int releaseY = absY;
                int moved = touchTotalMove;
                touchDragging = false;

                // If movement was small, treat as a tap -> fall through to click handling
                if (moved > 6)
                {
                    // it was a drag, nothing more to do
                    return;
                }
                // otherwise continue to treat as click; pos already holds release coordinates
            }
        }

        // At this point handle simple taps / clicks
        if (!Screen::isTouched())
        {
            if (!pos.clicked)
                return;

            // Top bar Exit (right)
            if (absY < TOPBAR_H && absX > (SCREEN_W - 60))
            {
                Exit();
                return;
            }

            // Top-left Home tapped: go home (safe within topbar)
            if (absY < TOPBAR_H && absX < 120)
            {
                loc.state = "home";
                ReRender();
                return;
            }

            // Home page buttons (two buttons)
            if (loc.state == "home")
            {
                int cardX = 6;
                int cardW = SCREEN_W - cardX * 2;
                int btnW = (cardW - BUTTON_PADDING * 3) / 2; // two buttons
                int bx0 = cardX + BUTTON_PADDING;
                int by = 22 + 6; // cardY + 6
                if (absY >= by && absY <= by + BUTTON_H)
                {
                    for (int i = 0; i < 2; ++i)
                    {
                        int bx = bx0 + i * (btnW + BUTTON_PADDING);
                        if (absX >= bx && absX <= bx + btnW)
                        {
                            if (i == 0)
                            {
                                // Open Site -> prompt for URL
                                String input = promptText("Which page do you want to visit?", "");
                                if (input.length() > 0)
                                {
                                    int idx = input.indexOf('@');
                                    String domainPort = input;
                                    String state = "startpage";
                                    if (idx > 0)
                                    {
                                        domainPort = input.substring(0, idx);
                                        state = input.substring(idx + 1);
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
                            else if (i == 1)
                            {
                                // Open Search
                                navigate("mw-search-server-onrender-app.onrender.com", 443, "startpage");
                            }
                            return;
                        }
                    }
                }
            }

            // Visits list interactions (delete / cleardata / open)
            // Use viewport-relative coordinates for hit-testing when viewport is active
            if (inViewport)
            {
                std::vector<String> sites = ENC_FS::BrowserStorage::listSites();
                if (!sites.empty())
                {
                    // localY is relative to viewport; compute item index
                    int idx = (localY + visitScrollOffset) / VISIT_ITEM_H;
                    if (idx >= 0 && idx < (int)sites.size())
                    {
                        String domain = sites[idx];
                        int btnW = 56;
                        int btnGap = 4;
                        int xOpen = vpW - BUTTON_PADDING - btnW; // viewport coords
                        int xClear = xOpen - btnGap - btnW;
                        int xDelete = xClear - btnGap - btnW;

                        // Delete
                        if (localX >= xDelete && localX <= xDelete + btnW)
                        {
                            ENC_FS::BrowserStorage::del(domain);
                            ReRender();
                            return;
                        }
                        // ClearData (set empty)
                        if (localX >= xClear && localX <= xClear + btnW)
                        {
                            ENC_FS::Buffer empty;
                            ENC_FS::BrowserStorage::set(domain, empty);
                            ReRender();
                            return;
                        }
                        // Open
                        if (localX >= xOpen && localX <= xOpen + btnW)
                        {
                            // Ask user for URL override or open directly
                            String theUrl = promptText("Which page do you want to visit?", domain);
                            if (theUrl.length() > 0)
                            {
                                int idxAt = theUrl.indexOf('@');
                                String domainPort = theUrl;
                                String state = "startpage";
                                if (idxAt > 0)
                                {
                                    domainPort = theUrl.substring(0, idxAt);
                                    state = theUrl.substring(idxAt + 1);
                                }
                                int colon = domainPort.indexOf(':');
                                String domainOnly = domainPort;
                                int port = 443;
                                if (colon > 0)
                                {
                                    domainOnly = domainPort.substring(0, colon);
                                    port = domainPort.substring(colon + 1).toInt();
                                }
                                navigate(domainOnly, port, state);
                            }
                            return;
                        }

                        // If tap on the item text area -> navigate directly
                        int textAreaW = vpW - BUTTON_PADDING - (3 * (btnW + btnGap));
                        if (localX >= 0 && localX <= textAreaW)
                        {
                            navigate(domain, 443, "startpage");
                            return;
                        }
                    }
                }
            }
            else
            {
                // Fallback: if viewport is not active but user tapped below VISIT_LIST_Y, handle in absolute coordinates
                if (absY >= VISIT_LIST_Y)
                {
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
                            String theUrl = promptText("Which page do you want to visit?", domain);
                            if (theUrl.length() > 0)
                            {
                                int idxAt = theUrl.indexOf('@');
                                String domainPort = theUrl;
                                String state = "startpage";
                                if (idxAt > 0)
                                {
                                    domainPort = theUrl.substring(0, idxAt);
                                    state = theUrl.substring(idxAt + 1);
                                }
                                int colon = domainPort.indexOf(':');
                                String domainOnly = domainPort;
                                int port = 443;
                                if (colon > 0)
                                {
                                    domainOnly = domainPort.substring(0, colon);
                                    port = domainPort.substring(colon + 1).toInt();
                                }
                                navigate(domainOnly, port, state);
                            }
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
            }

            // Settings page taps: top-left back (kept for compatibility)
            if (loc.state == "settings")
            {
                if (absY > TOPBAR_H && absY < TOPBAR_H + 30 && absX < 120)
                {
                    loc.state = "home";
                    ReRender();
                }
            }
        }
    }

    void navigate(const String &domain, int port, const String &state)
    {
        // set desired location
        loc.domain = domain;
        loc.port = port;
        loc.state = state;
        saveVisitedSite(domain);

        // Connect websocket to the target domain (only when user requested)
        // Disconnect previous socket first (if any)
        webSocket.disconnect();
        // Begin SSL WebSocket connection to server
        webSocket.beginSSL(loc.domain.c_str(), loc.port, "/");
        webSocket.setReconnectInterval(5000);

        // Re-render UI to show website title bar and view
        ReRender();
    }

    // Save visited site into BrowserStorage (simple presence marker)
    void saveVisitedSite(const String &domain)
    {
        unsigned long ts = millis();
        String tsStr = String(ts);
        ENC_FS::Buffer buf((uint8_t *)tsStr.c_str(), (uint8_t *)tsStr.c_str() + tsStr.length());
        ENC_FS::BrowserStorage::set(domain, buf);
    }

    void showHomeUI()
    {
        // floating card style: small shadow box for the top controls
        int cardX = 6;
        int cardY = 25;
        int cardW = SCREEN_W - cardX * 2;
        int cardH = 48;

        // outer (shadow / primary)
        Screen::tft.fillRoundRect(cardX, cardY, cardW, cardH, 6, theme.primary);
        // inner (floating)
        Screen::tft.fillRoundRect(cardX + 2, cardY + 2, cardW - 4, cardH - 4, 6, theme.bg);

        int btnW = (cardW - BUTTON_PADDING * 3) / 2;
        int bx = cardX + BUTTON_PADDING;
        int by = cardY + 6;

        // Button 0: Open Site
        Screen::tft.fillRoundRect(bx, by, btnW, BUTTON_H, 6, theme.accent);
        drawText(bx + 8, by + 8, "Open Site", theme.accentText, 2);

        // Button 1: Open Search
        bx += btnW + BUTTON_PADDING;
        Screen::tft.fillRoundRect(bx, by, btnW, BUTTON_H, 6, theme.accent2);
        drawText(bx + 8, by + 8, "Open Search", theme.accentText, 2);
    }

    void showVisitedSites()
    {
        std::vector<String> sites = ENC_FS::BrowserStorage::listSites();

        // heading
        drawText(10, VISIT_LIST_Y - 18, "Visited Sites", theme.text, 2);

        // create clipping viewport for the list area
        enterViewport(0, VISIT_LIST_Y, SCREEN_W, SCREEN_H - VISIT_LIST_Y);

        int xText = 10;

        int totalHeight = (int)sites.size() * VISIT_ITEM_H;
        int visible = vpH; // SCREEN_H - VISIT_LIST_Y;
        int maxOffset = totalHeight > visible ? totalHeight - visible : 0;
        if (visitScrollOffset < 0)
            visitScrollOffset = 0;
        if (visitScrollOffset > maxOffset)
            visitScrollOffset = maxOffset;

        for (size_t i = 0; i < sites.size(); ++i)
        {
            int localY = (int)i * VISIT_ITEM_H - visitScrollOffset; // relative to viewport

            // skip items fully above or below viewport
            if (localY + VISIT_ITEM_H < 0)
                continue;
            if (localY > vpH)
                continue;

            // background alternating for clarity
            uint16_t bgColor = (i % 2 == 0) ? theme.primary : theme.bg;

            // clip background if partially visible
            int drawY = localY;
            int drawH = VISIT_ITEM_H - 2;
            if (drawY < 0)
            {
                drawH += drawY; // reduce height
                drawY = 0;
            }
            if (drawY + drawH > vpH)
                drawH = vpH - drawY;

            if (drawH > 0)
                Screen::tft.fillRect(0, drawY, vpW, drawH, bgColor);

            // domain text (truncate if too long)
            String domain = sites[i];
            int maxDomainWidth = vpW - 180; // Leave space for buttons
            if (domain.length() > maxDomainWidth / 6)
            { // Approx 6 pixels per char
                domain = domain.substring(0, maxDomainWidth / 6 - 3) + "...";
            }

            int textY = localY + 6;
            if (textY < 0)
                textY = 0; // ensure visible
            drawText(xText, textY, domain, theme.text, 1);

            // buttons on the right: Delete, ClearData, Open
            int btnW = 56;
            int btnGap = 4;
            int xOpen = vpW - BUTTON_PADDING - btnW;
            int xClear = xOpen - btnGap - btnW;
            int xDelete = xClear - btnGap - btnW;

            int btnY = localY + 4;
            int btnH = VISIT_ITEM_H - 10;

            // ensure button area is within viewport
            if (btnY < 0)
            {
                // partially visible: adjust height and y
                int visibleBtnH = btnH + btnY;
                if (visibleBtnH > 0)
                {
                    Screen::tft.fillRoundRect(xDelete, 0, btnW, visibleBtnH, 4, theme.danger);
                    drawText(xDelete + 8, 0 + 2, "Delete", theme.accentText, 1);
                    Screen::tft.fillRoundRect(xClear, 0, btnW, visibleBtnH, 4, theme.pressed);
                    drawText(xClear + 6, 0 + 2, "Clear", theme.accentText, 1);
                    Screen::tft.fillRoundRect(xOpen, 0, btnW, visibleBtnH, 4, theme.accent);
                    drawText(xOpen + 10, 0 + 2, "Open", theme.accentText, 1);
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

        // reset viewport
        exitViewport();
    }

    void showSettingsPage()
    {
        Screen::tft.fillScreen(theme.bg);
        renderTopBar();
        drawText(10, 30, "Visited Sites & Storage", theme.text, 2);

        // small instruction
        drawText(10, 56, "Tap a site to open. Use buttons to manage data.", theme.placeholder, 1);

        // list sites below
        showVisitedSites();
    }

    void showOSSearchPage()
    {
        navigate("mw-search-server-onrender-app.onrender.com", 443, "startpage");
    }

    void showInputPage()
    {
        String input = promptText("Enter domain@state", "example.com@startpage");
        int idx = input.indexOf('@');
        if (idx > 0)
        {
            String domainPort = input.substring(0, idx);
            String state = input.substring(idx + 1);
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
        else if (input.length() > 0)
        {
            // default to startpage state
            int colon = input.indexOf(':');
            String domain = input;
            int port = 443;
            if (colon > 0)
            {
                domain = input.substring(0, colon);
                port = input.substring(colon + 1).toInt();
            }
            navigate(domain, port, "startpage");
        }
    }

    void showWebsitePage()
    {
        // Top bar with title and exit button (redraw topbar to show title)
        Screen::tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, theme.primary);

        String title = loc.title.length() ? loc.title : loc.domain;
        // Truncate title if too long
        if (title.length() > 20)
        {
            title = title.substring(0, 17) + "...";
        }
        drawText(6, 3, title, theme.accentText, 2);
        drawText(SCREEN_W - 22, 3, "X", theme.danger, 2);

        // Set viewport for website content (prevents drawing in top bar area)
        enterViewport(0, VIEWPORT_Y, SCREEN_W, VIEWPORT_H);

        // Clear website area with bg color (inside viewport coordinates)
        Screen::tft.fillRect(0, 0, vpW, vpH, theme.bg);

        // Draw a small hint inside the viewport (top-left)
        drawText(6, 4, "Page view", theme.placeholder, 1);

        // Reset viewport to full screen for other operations
        exitViewport();
    }

} // namespace Browser
