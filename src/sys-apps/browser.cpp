#include "browser.hpp"
#include <HTTPClient.h>
#include <SPIFFS.h>
#include <Arduino.h>
#include <vector>
#include <algorithm>

using std::vector;

namespace Browser
{

    const int TOP_BAR_HEIGHT = 20;
    const int VIEWPORT_WIDTH = TFT_WIDTH;
    const int VIEWPORT_HEIGHT = TFT_HEIGHT - TOP_BAR_HEIGHT;

    String Location::sessionId = String(random(0xFFFFFFFF), HEX);
    Location loc;
    bool isRunning = false;
    String currentInput = "";
    bool inputActive = false;
    int inputCursorPos = 0;
    int ScrollX = 0, ScrollY = 0;

    // Pending server responses
    struct PendingResponse
    {
        String returnId;
        String type; // "session", "state", "text"
    };
    vector<PendingResponse> pendingResponses;

    // Utility: compute URL input area
    static void computeUrlInputArea(int &outX, int &outW)
    {
        const int x = 65;
        int w = std::max(VIEWPORT_WIDTH - 140, 60);
        outX = x;
        outW = w;
    }

    // --- Rendering commands ---
    void handleFillRect(const String &params)
    {
        int x, y, w, h, color;
        sscanf(params.c_str(), "%d %d %d %d %d", &x, &y, &w, &h, &color);
        tft.fillRect(x + ScrollX, y + ScrollY + TOP_BAR_HEIGHT, w, h, color);
    }

    void handleDrawString(const String &params)
    {
        int x, y, color;
        int quoteStart = params.indexOf('"');
        int quoteEnd = params.lastIndexOf('"');
        if (quoteStart == -1 || quoteEnd == -1)
            return;
        String text = params.substring(quoteStart + 1, quoteEnd);
        sscanf(params.substring(0, quoteStart).c_str(), "%d %d %d", &x, &y, &color);
        tft.setTextColor(color);
        tft.setCursor(x + ScrollX, y + ScrollY + TOP_BAR_HEIGHT);
        tft.print(text);
    }

    void handleDrawLine(const String &params)
    {
        int x1, y1, x2, y2, color;
        sscanf(params.c_str(), "%d %d %d %d %d", &x1, &y1, &x2, &y2, &color);
        tft.drawLine(x1 + ScrollX, y1 + ScrollY + TOP_BAR_HEIGHT,
                     x2 + ScrollX, y2 + ScrollY + TOP_BAR_HEIGHT, color);
    }

    void handleDrawRect(const String &params)
    {
        int x, y, w, h, color;
        sscanf(params.c_str(), "%d %d %d %d %d", &x, &y, &w, &h, &color);
        tft.drawRect(x + ScrollX, y + ScrollY + TOP_BAR_HEIGHT, w, h, color);
    }

    void handlePushSVG(const String &params)
    {
        int x, y, w, h;
        int quoteStart = params.indexOf('"');
        int quoteEnd = params.lastIndexOf('"');
        if (quoteStart == -1 || quoteEnd == -1)
            return;
        String svgData = params.substring(quoteStart + 1, quoteEnd);
        sscanf(params.substring(0, quoteStart).c_str(), "%d %d %d %d", &x, &y, &w, &h);
        drawSVGString(svgData, x + ScrollX, y + ScrollY + TOP_BAR_HEIGHT, w, h, TFT_WHITE);
    }

    void handleClearScreen(const String &params)
    {
        uint16_t color = params.toInt();
        tft.fillRect(0, TOP_BAR_HEIGHT, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, color);
    }

    void handleSetTextSize(const String &params)
    {
        tft.setTextSize(params.toInt());
    }

    void handleSetCursor(const String &params)
    {
        int x, y;
        sscanf(params.c_str(), "%d %d", &x, &y);
        tft.setCursor(x + ScrollX, y + ScrollY + TOP_BAR_HEIGHT);
    }

    void handleSetTextColor(const String &params)
    {
        tft.setTextColor(params.toInt());
    }

    // --- Top bar ---
    void drawTopBar()
    {
        tft.fillRect(0, 0, VIEWPORT_WIDTH, TOP_BAR_HEIGHT, Style::Colors::primary);
        // Back button
        tft.fillTriangle(5, 10, 15, 5, 15, 15, Style::Colors::accentText);
        // Forward button
        tft.fillTriangle(25, 5, 25, 15, 35, 10, Style::Colors::accentText);
        // Refresh
        tft.fillCircle(50, 10, 6, Style::Colors::accentText);
        // URL input
        int urlX, urlW;
        computeUrlInputArea(urlX, urlW);
        tft.fillRect(urlX, 5, urlW, 10, Style::Colors::bg);
        tft.drawRect(urlX, 5, urlW, 10, Style::Colors::accent);
        tft.setTextColor(Style::Colors::text);
        tft.setCursor(urlX + 2, 7);
        tft.print(loc.domain);
        // Exit button
        tft.fillRect(VIEWPORT_WIDTH - 70, 5, 30, 10, Style::Colors::danger);
        tft.setTextColor(Style::Colors::accentText);
        tft.setCursor(VIEWPORT_WIDTH - 65, 7);
        tft.print("Exit");
    }

    // --- Touch handling ---
    void handleTouchInTopBar(int x, int y)
    {
        if (y > TOP_BAR_HEIGHT)
            return;
        if (x >= 5 && x <= 20)
        { /* TODO: back */
        }
        else if (x >= 25 && x <= 40)
        { /* TODO: forward */
        }
        else if (x >= 45 && x <= 55)
        {
            fetchPage(loc.domain);
        } // refresh
        else if (x >= VIEWPORT_WIDTH - 70 && x <= VIEWPORT_WIDTH - 40)
        {
            Exit();
        }
    }

    // --- Server parser ---
    void parseServerCommand(const String &line)
    {
        int colon = line.indexOf(':');
        if (colon == -1)
            return;
        String cmd = line.substring(0, colon);
        String params = line.substring(colon + 1);
        params.trim();

        if (cmd == "FillRect")
            handleFillRect(params);
        else if (cmd == "DrawString")
            handleDrawString(params);
        else if (cmd == "DrawLine")
            handleDrawLine(params);
        else if (cmd == "DrawRect")
            handleDrawRect(params);
        else if (cmd == "PushSVG")
            handlePushSVG(params);
        else if (cmd == "ClearScreen")
            handleClearScreen(params);
        else if (cmd == "SetTextSize")
            handleSetTextSize(params);
        else if (cmd == "SetCursor")
            handleSetCursor(params);
        else if (cmd == "SetTextColor")
            handleSetTextColor(params);
    }

    // --- HTTP fetch ---
    void fetchPage(const String &url)
    {
        HTTPClient http;
        String fullUrl = "http://" + url + "/@state";
        http.begin(fullUrl);

        if (loc.session.length() > 0)
        {
            http.addHeader("Cookie", "sessionId=" + loc.session);
        }

        int code = http.GET();
        if (code == 200)
        {
            String payload = http.getString();
            int newline = 0;
            while (newline < payload.length())
            {
                int next = payload.indexOf('\n', newline);
                if (next == -1)
                    next = payload.length();
                String line = payload.substring(newline, next);
                parseServerCommand(line);
                newline = next + 1;
            }

            // Save session from Set-Cookie
            String setCookie = http.header("Set-Cookie");
            if (setCookie.length() > 0)
            {
                loc.session = setCookie;
                SPIFFS.begin();
                File f = SPIFFS.open("/browser_" + loc.domain, FILE_WRITE);
                if (f)
                {
                    f.print(loc.session);
                    f.close();
                }
            }
        }
        http.end();
    }

    // --- Touch input ---
    void handleTouchInput()
    {
        if (!Screen::isTouched())
            return;
        Screen::TouchPos t = Screen::getTouchPos();
        if (t.y <= TOP_BAR_HEIGHT)
            handleTouchInTopBar(t.x, t.y);
        else if (t.clicked)
        {
            // Send click to server
            // HTTP is stateless, so clicks just trigger refresh
            fetchPage(loc.domain);
        }
    }

    // --- Keyboard ---
    void handleKeyboardInput()
    {
        if (Serial.available())
        {
            String in = Serial.readStringUntil('\n');
            in.trim();
            if (in.length() > 0)
            {
                // Input dialog to server
                pendingResponses.push_back({String(random(0xFFFF), HEX), "text"});
            }
        }
    }

    // --- Main update ---
    void Update()
    {
        handleTouchInput();
        handleKeyboardInput();
    }

    // --- Start / Exit ---
    void Start(const String &url)
    {
        loc.domain = url;
        loc.state = "startpage";

        // Load session if exists
        if (SPIFFS.begin())
        {
            File f = SPIFFS.open("/browser_" + loc.domain, FILE_READ);
            if (f)
            {
                loc.session = f.readString();
                f.close();
            }
        }

        drawTopBar();
        fetchPage(loc.domain);
        isRunning = true;
    }

    void Start() { Start("mw-search-server-onrender-app.onrender.com"); }

    void Exit() { isRunning = false; }

    void OnExit()
    {
        tft.fillScreen(Style::Colors::bg);
        if (loc.session.length() > 0)
        {
            SPIFFS.begin();
            File f = SPIFFS.open("/browser_" + loc.domain, FILE_WRITE);
            if (f)
            {
                f.print(loc.session);
                f.close();
            }
        }
    }

} // namespace Browser

// --- Main entry ---
void openBrowser()
{
    Browser::Start();
    while (Browser::isRunning)
    {
        Browser::Update();
        vTaskDelay(10);
    }
    Browser::OnExit();
}

void openBrowser(const String &url)
{
    Browser::Start(url);
    while (Browser::isRunning)
    {
        Browser::Update();
        vTaskDelay(10);
    }
    Browser::OnExit();
}
