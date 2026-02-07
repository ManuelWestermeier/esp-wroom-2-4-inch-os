#include "browser.hpp"
#include <WiFiClientSecure.h>

namespace Browser
{
    Location loc;
    bool isRunning = false;
    WebSocketsClient webSocket;
    String Location::sessionId = String(random(100000, 999999));

    void Start()
    {
        tft.fillScreen(TFT_BLACK);
        drawTopBar();
        showHomeButtons();

        webSocket.beginSSL(loc.domain.c_str(), loc.port, "/");
        webSocket.setReconnectInterval(5000);

        webSocket.onEvent([](WStype_t type, uint8_t *payload, size_t length)
                          {
            String msg = (payload != nullptr) ? (char *)payload : "";
            switch (type)
            {
                case WStype_CONNECTED:
                    Serial.println("[Browser] Connected");
                    webSocket.sendTXT("MWOSP-v1 " + Location::sessionId + " 320 480");
                    break;
                case WStype_TEXT:
                    handleCommand(msg);
                    break;
                case WStype_DISCONNECTED:
                    Serial.println("[Browser] Disconnected");
                    break;
            } });
    }

    void handleCommand(const String &payload)
    {
        // ----------------- TFT Drawing -----------------
        if (payload.startsWith("FillRect"))
        {
            int x, y, w, h;
            uint16_t c;
            if (sscanf(payload.c_str(), "FillRect %d %d %d %d %hu", &x, &y, &w, &h, &c) == 5)
                tft.fillRect(x, y, w, h, c);
        }
        else if (payload.startsWith("DrawCircle"))
        {
            int x, y, r;
            uint16_t c;
            if (sscanf(payload.c_str(), "DrawCircle %d %d %d %hu", &x, &y, &r, &c) == 4)
                drawCircle(x, y, r, c);
        }
        else if (payload.startsWith("DrawText"))
        {
            int x, y, size;
            uint16_t c;
            char buf[256];
            if (sscanf(payload.c_str(), "DrawText %d %d %d %hu %[^\n]", &x, &y, &size, &c, buf) >= 5)
                drawText(x, y, String(buf), c, size);
        }
        else if (payload.startsWith("DrawSVG"))
        {
            int x, y, w, h;
            uint16_t c;
            int idx = payload.indexOf(' ', 7);
            String svg = payload.substring(idx + 1);
            drawSVG(svg, x, y, w, h, c);
        }
        // ----------------- Storage -----------------
        else if (payload.startsWith("GetStorage"))
        {
            String key = payload.substring(11);
            ENC_FS::Buffer data = loadData(key);
            webSocket.sendTXT("GetBackStorage " + key + " " + String((const char *)data.data(), data.size()));
        }
        else if (payload.startsWith("SetStorage"))
        {
            int idx = payload.indexOf(' ', 11);
            String key = payload.substring(11, idx);
            String val = payload.substring(idx + 1);
            ENC_FS::Buffer buf((uint8_t *)val.c_str(), (uint8_t *)val.c_str() + val.length());
            storeData(key, buf);
        }
        // ----------------- Navigation & Control -----------------
        else if (payload.startsWith("Navigate"))
        {
            int idx1 = payload.indexOf('@', 9);
            int idx2 = payload.indexOf(':', 9);
            String domain;
            int port = 443;
            String state;
            if (idx2 > 0 && idx2 < idx1)
            {
                domain = payload.substring(9, idx2);
                port = payload.substring(idx2 + 1, idx1).toInt();
            }
            else
                domain = payload.substring(9, idx1);
            state = payload.substring(idx1 + 1);
            navigate(domain, port, state);
        }
        else if (payload.startsWith("Exit"))
        {
            Exit();
        }
        else if (payload.startsWith("ClearSettings"))
        {
            clearSettings();
        }
        else if (payload.startsWith("PromptText"))
        {
            String rid = payload.substring(11);
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
    }

    void Update()
    {
        webSocket.loop();
        handleTouch();
    }

    void ReRender()
    {
        tft.fillScreen(TFT_BLACK);
        drawTopBar();
        if (loc.state == "home")
            showHomeButtons();
        else if (loc.state == "settings")
            showSettingsPage();
        else if (loc.state == "search")
            showOSSearchPage();
        else if (loc.state == "input")
            showInputPage();
    }

    void Exit()
    {
        isRunning = false;
    }

    void OnExit()
    {
        webSocket.disconnect();
    }

    // ----------------- Utilities -----------------
    void drawText(int x, int y, const String &text, uint16_t color, int size)
    {
        tft.setTextColor(color);
        tft.setTextSize(size);
        tft.setCursor(x, y);
        tft.print(text);
    }

    void drawCircle(int x, int y, int r, uint16_t color)
    {
        tft.drawCircle(x, y, r, color);
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
        // ENC_FS::BrowserStorage::del("settings");
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

    void drawTopBar()
    {
        tft.fillRect(0, 0, 320, 20, Style::Colors::primary);
        drawText(5, 3, loc.domain + "@" + loc.state, TFT_WHITE, 1);
        drawText(280, 3, "X", TFT_RED, 2); // Exit button
    }

    void handleTouch()
    {
        if (!Screen::isTouched())
            return;
        Screen::TouchPos pos = Screen::getTouchPos();
        if (!pos.clicked)
            return;

        // Top bar exit
        if (pos.y < 20 && pos.x > 275)
        {
            Exit();
            return;
        }

        // Home page buttons
        if (loc.state == "home")
        {
            if (pos.y > 50 && pos.y < 90)
            {
                if (pos.x > 10 && pos.x < 150)
                {
                    loc.state = "settings";
                    ReRender();
                }
                else if (pos.x > 160 && pos.x < 310)
                {
                    loc.state = "search";
                    ReRender();
                }
            }
            else if (pos.y > 100 && pos.y < 140)
            {
                loc.state = "input";
                ReRender();
            }
        }
    }

    void navigate(const String &domain, int port, const String &state)
    {
        loc.domain = domain;
        loc.port = port;
        loc.state = state;
        ReRender();
        webSocket.beginSSL(loc.domain.c_str(), loc.port, "/");
    }

    void showHomeButtons()
    {
        tft.fillRect(10, 50, 140, 40, Style::Colors::accent);
        drawText(20, 60, "Settings", TFT_WHITE, 2);

        tft.fillRect(160, 50, 140, 40, Style::Colors::accent);
        drawText(170, 60, "OS-Search-Page", TFT_WHITE, 2);

        tft.fillRect(10, 100, 290, 40, Style::Colors::primary);
        drawText(20, 110, "Input Page", TFT_WHITE, 2);
    }

    void showSettingsPage()
    {
        tft.fillScreen(Style::Colors::bg);
        drawText(10, 30, "Visited Sites & Storage", TFT_WHITE, 2);
        // TODO: list sites and allow deletion
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
    }
}
