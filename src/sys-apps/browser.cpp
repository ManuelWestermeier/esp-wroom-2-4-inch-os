// browser.cpp
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
    static constexpr int TOPBAR_H = 20;
    static constexpr int BUTTON_ROW_Y = 30;
    static constexpr int BUTTON_H = 36;
    static constexpr int BUTTON_PADDING = 10;
    static constexpr int VISIT_LIST_Y = 80;
    static constexpr int VISIT_ITEM_H = 30;
    static constexpr int VIEWPORT_Y = TOPBAR_H;
    static constexpr int VIEWPORT_H = SCREEN_H - TOPBAR_H;

    // Forward declarations
    void renderTopBar();
    void showHomeUI();
    void showVisitedSites();
    void showWebsitePage();
    void ReRender();

    // ---- Start / lifecycle ----
    void Start()
    {
        // Render initial UI but do NOT connect the websocket automatically.
        // The remote website connection will be opened only after the user explicitly navigates.
        Screen::tft.fillScreen(BG);
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
                Serial.println("[Browser] Connected");
                // Send handshake with sessionId and device resolution (width x height)
                webSocket.sendTXT("MWOSP-v1 " + Location::sessionId + " " + String(SCREEN_W) + " " + String(SCREEN_H));
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
        // ----------------- TFT Drawing -----------------
        if (payload.startsWith("FillRect"))
        {
            int x = 0, y = 0, w = 0, h = 0;
            unsigned int c = 0;
            if (sscanf(payload.c_str(), "FillRect %d %d %d %d %u", &x, &y, &w, &h, &c) == 5)
            {
                // Guard drawing to visible area to avoid out-of-bounds writes.
                if (w > 0 && h > 0 && x < SCREEN_W && y < SCREEN_H && x + w > 0 && y + h > 0)
                    Screen::tft.fillRect(x, y, w, h, (uint16_t)c);
            }
        }
        else if (payload.startsWith("DrawCircle"))
        {
            int x = 0, y = 0, r = 0;
            unsigned int c = 0;
            if (sscanf(payload.c_str(), "DrawCircle %d %d %d %u", &x, &y, &r, &c) == 4)
            {
                if (r >= 0 && x >= -r && y >= -r && x <= SCREEN_W + r && y <= SCREEN_H + r)
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
                // Ensure y within screen bounds
                if (y >= 0 && y < SCREEN_H)
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
                    // Ensure svg area intersects the screen
                    if (w > 0 && h > 0 && x < SCREEN_W && y < SCREEN_H && x + w > 0 && y + h > 0)
                        drawSVG(svg, x, y, w, h, (uint16_t)c);
                }
            }
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
        Screen::tft.fillScreen(BG);
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
        Screen::tft.setTextColor(color);
        Screen::tft.setTextSize(size);
        Screen::tft.setCursor(x, y);
        Screen::tft.print(text);
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
            return Style::Colors::bg;
        if (name == "text")
            return Style::Colors::text;
        if (name == "primary")
            return Style::Colors::primary;
        if (name == "accent")
            return Style::Colors::accent;
        if (name == "accent2")
            return Style::Colors::accent2;
        if (name == "accent3")
            return Style::Colors::accent3;
        if (name == "accentText")
            return Style::Colors::accentText;
        if (name == "pressed")
            return Style::Colors::pressed;
        if (name == "danger")
            return Style::Colors::danger;
        if (name == "placeholder")
            return Style::Colors::placeholder;
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
        // top bar background
        Screen::tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, PRIMARY);

        // left: time display
        // Acquire time from UserTime (if available)
        String timeStr = "XX:XX";
        // If your project defines USER_TIME_AVAILABLE and provides UserTime::get()
        auto time = UserTime::get();
        String hour = String(time.tm_hour);
        String minute = String(time.tm_min);
        if (minute.length() < 2)
            minute = "0" + minute;
        if (hour.length() < 2)
            hour = "0" + hour;
        if (time.tm_year == 0)
            timeStr = "XX:XX";
        else
            timeStr = hour + ":" + minute;

        drawText(6, 3, timeStr, TEXT, 2);

        // left label next to time
        drawText(50, 3, "MW-OS-Browser", TEXT, 2);

        // close button on top right
        drawText(SCREEN_W - 22, 3, "X", DANGER, 2);
    }

    void handleTouch()
    {
        if (!Screen::isTouched())
            return;
        Screen::TouchPos pos = Screen::getTouchPos();
        if (!pos.clicked)
            return;

        // Top bar close button (return to menu)
        if (pos.y < TOPBAR_H && pos.x > (SCREEN_W - 30))
        {
            // Close browser / go back to menu
            Exit();
            return;
        }

        // Top-left title tapped: go home (safe within topbar)
        if (pos.y < TOPBAR_H && pos.x < 120)
        {
            loc.state = "home";
            ReRender();
            return;
        }

        // Home page buttons (three buttons in a row)
        if (loc.state == "home")
        {
            // button positions (same layout used in showHomeUI)
            int cardX = 6;
            int cardW = SCREEN_W - cardX * 2;
            int btnW = (cardW - BUTTON_PADDING * 4) / 3;
            int bx0 = cardX + BUTTON_PADDING;
            int by = 22 + 6; // cardY + 6
            // Search/Input/Settings detection
            if (pos.y >= by && pos.y <= by + BUTTON_H)
            {
                for (int i = 0; i < 3; ++i)
                {
                    int bx = bx0 + i * (btnW + BUTTON_PADDING);
                    if (pos.x >= bx && pos.x <= bx + btnW)
                    {
                        if (i == 0)
                        {
                            // Search -> open known search server (connect only after click)
                            navigate("mw-search-server-onrender-app.onrender.com", 443, "startpage");
                        }
                        else if (i == 1)
                        {
                            // Input Url
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
                        else if (i == 2)
                        {
                            // Settings
                            loc.state = "settings";
                            ReRender();
                        }
                        return;
                    }
                }
            }
        }

        // Visits list interactions (delete / cleardata / open)
        if (pos.y >= VISIT_LIST_Y)
        {
            std::vector<String> sites = ENC_FS::BrowserStorage::listSites();
            int idx = (pos.y - VISIT_LIST_Y) / VISIT_ITEM_H;
            if (idx >= 0 && idx < (int)sites.size())
            {
                String domain = sites[idx];
                int btnW = 56;
                int btnGap = 4;
                int xOpen = SCREEN_W - BUTTON_PADDING - btnW;
                int xClear = xOpen - btnGap - btnW;
                int xDelete = xClear - btnGap - btnW;

                // Delete
                if (pos.x >= xDelete && pos.x <= xDelete + btnW)
                {
                    ENC_FS::BrowserStorage::del(domain);
                    ReRender();
                    return;
                }
                // ClearData (set empty)
                if (pos.x >= xClear && pos.x <= xClear + btnW)
                {
                    ENC_FS::Buffer empty;
                    ENC_FS::BrowserStorage::set(domain, empty);
                    ReRender();
                    return;
                }
                // Open
                if (pos.x >= xOpen && pos.x <= xOpen + btnW)
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
                int textAreaW = SCREEN_W - BUTTON_PADDING - (3 * (btnW + btnGap));
                if (pos.x >= 0 && pos.x <= textAreaW)
                {
                    navigate(domain, 443, "startpage");
                    return;
                }
            }
        }

        // Settings page taps: top-left back
        if (loc.state == "settings")
        {
            if (pos.y > TOPBAR_H && pos.y < TOPBAR_H + 30 && pos.x < 120)
            {
                loc.state = "home";
                ReRender();
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
        int cardY = 22;
        int cardW = SCREEN_W - cardX * 2;
        int cardH = 60;

        // outer (shadow / primary)
        Screen::tft.fillRoundRect(cardX, cardY, cardW, cardH, 6, PRIMARY);
        // inner (floating)
        Screen::tft.fillRoundRect(cardX + 2, cardY + 2, cardW - 4, cardH - 4, 6, BG);

        int btnW = (cardW - BUTTON_PADDING * 4) / 3;
        int bx = cardX + BUTTON_PADDING;
        int by = cardY + 6;

        // Button 0: Search
        Screen::tft.fillRoundRect(bx, by, btnW, BUTTON_H, 6, ACCENT);
        drawText(bx + 8, by + 8, "Search", AT, 2);

        // Button 1: Input Url
        bx += btnW + BUTTON_PADDING;
        Screen::tft.fillRoundRect(bx, by, btnW, BUTTON_H, 6, ACCENT2);
        drawText(bx + 8, by + 8, "Input URL", AT, 2);

        // Button 2: Settings
        bx += btnW + BUTTON_PADDING;
        Screen::tft.fillRoundRect(bx, by, btnW, BUTTON_H, 6, ACCENT3);
        drawText(bx + 8, by + 8, "Settings", AT, 2);
    }

    void showVisitedSites()
    {
        std::vector<String> sites = ENC_FS::BrowserStorage::listSites();

        // heading
        drawText(10, VISIT_LIST_Y - 18, "Visited Sites", TEXT, 2);

        // render list items
        int xText = 10;
        for (size_t i = 0; i < sites.size(); ++i)
        {
            int y = VISIT_LIST_Y + i * VISIT_ITEM_H;
            // clip vertical list to not draw beyond screen
            if (y + VISIT_ITEM_H - 2 > SCREEN_H)
                break;

            // background alternating for clarity
            uint16_t bgColor = (i % 2 == 0) ? Style::Colors::primary : BG;
            Screen::tft.fillRect(0, y, SCREEN_W, VISIT_ITEM_H - 2, bgColor);

            // domain text
            drawText(xText, y + 6, sites[i], TEXT, 1);

            // buttons on the right: Delete, ClearData, Open
            int btnW = 56;
            int btnGap = 4;
            int xOpen = SCREEN_W - BUTTON_PADDING - btnW;
            int xClear = xOpen - btnGap - btnW;
            int xDelete = xClear - btnGap - btnW;

            Screen::tft.fillRoundRect(xDelete, y + 4, btnW, VISIT_ITEM_H - 10, 4, DANGER);
            drawText(xDelete + 8, y + 6, "Delete", AT, 1);

            Screen::tft.fillRoundRect(xClear, y + 4, btnW, VISIT_ITEM_H - 10, 4, PRESSED);
            drawText(xClear + 6, y + 6, "Clear", AT, 1);

            Screen::tft.fillRoundRect(xOpen, y + 4, btnW, VISIT_ITEM_H - 10, 4, ACCENT);
            drawText(xOpen + 10, y + 6, "Open", AT, 1);
        }
    }

    void showSettingsPage()
    {
        Screen::tft.fillScreen(BG);
        renderTopBar();
        drawText(10, 30, "Visited Sites & Storage", TEXT, 2);

        // small instruction
        drawText(10, 56, "Tap a site to open. Use buttons to manage data.", PH, 1);

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
        // Top bar with title and close button (redraw topbar to show title)
        Screen::tft.fillRect(0, 0, SCREEN_W, TOPBAR_H, PRIMARY);

        String title = loc.title.length() ? loc.title : loc.domain;
        // left: time + app name handled in renderTopBar; here override title only if present
        drawText(6, 3, title, AT, 2);
        drawText(SCREEN_W - 22, 3, "X", DANGER, 2);

// Ensure viewport/clipping is set so drawing from server remains inside the page view.
// Requested viewport (0, TOPBAR_H, SCREEN_W, VIEWPORT_H)
// Many TFT libraries offer setViewport or setAddrWindow. We call setViewport as requested.
// If your TFT implementation lacks setViewport, replace this call with the appropriate clipping API.
#if defined(TFT_ESPI_VERSION) || defined(TFT_eSPI_h)
        // Best-effort call — if setViewport exists, use it.
        // NOTE: If your TFT library does not provide setViewport, comment out the next line.
        Screen::tft.setViewport(0, VIEWPORT_Y, SCREEN_W, VIEWPORT_H);
#endif

        // Clear website area with bg color (only inside viewport)
        Screen::tft.fillRect(0, VIEWPORT_Y, SCREEN_W, VIEWPORT_H, BG);

        // Draw a small hint inside the viewport (top-left)
        drawText(6, VIEWPORT_Y + 4, "Page view", PH, 1);
    }

} // namespace Browser
