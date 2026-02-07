// browser.cpp
#include "browser.hpp"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include <Arduino.h> // for random(), millis()
using std::vector;

namespace Browser
{
    // Top bar height must be defined before viewport calc
    const int TOP_BAR_HEIGHT = 20;

    String Location::sessionId = String(random(0xFFFFFFFF), HEX);
    Location loc;
    bool isRunning = false;
    WebSocketsClient webSocket;
    bool wsConnected = false;
    unsigned long lastReconnectAttempt = 0;
    const unsigned long RECONNECT_INTERVAL = 5000;

    // Screen dimensions
    const int VIEWPORT_WIDTH = TFT_WIDTH;
    const int VIEWPORT_HEIGHT = TFT_HEIGHT - TOP_BAR_HEIGHT; // Reserve space for top bar

    // Rendering state
    struct PendingResponse
    {
        String returnId;
        String type; // "session", "state", "text"
    };
    std::vector<PendingResponse> pendingResponses;

    // UI Elements
    String currentInput = "";
    bool inputActive = false;
    int inputCursorPos = 0;

    // Helper: returns computed URL input area (x, width)
    static void computeUrlInputArea(int &outX, int &outW)
    {
        const int x = 65;
        // Keep at least 60px width for URL textbox to avoid overlap on narrow screens
        int w = std::max(VIEWPORT_WIDTH - 140, 60);
        outX = x;
        outW = w;
    }

    // Server command handlers
    void handleFillRect(const String &params)
    {
        // Format: FillRect X Y W H COLOR
        int paramsArray[4];
        int color;

        int startIdx = 0;
        for (int i = 0; i < 4; i++)
        {
            int spaceIdx = params.indexOf(' ', startIdx);
            if (spaceIdx == -1)
                return; // malformed
            paramsArray[i] = params.substring(startIdx, spaceIdx).toInt();
            startIdx = spaceIdx + 1;
        }
        color = params.substring(startIdx).toInt();

        tft.fillRect(paramsArray[0], paramsArray[1] + TOP_BAR_HEIGHT,
                     paramsArray[2], paramsArray[3], color);
    }

    void handleDrawString(const String &params)
    {
        // Format: DrawString X Y COLOR "text"
        int firstSpace = params.indexOf(' ');
        int secondSpace = params.indexOf(' ', firstSpace + 1);
        int thirdSpace = params.indexOf(' ', secondSpace + 1);
        int quoteStart = params.indexOf('"');
        int quoteEnd = params.lastIndexOf('"');

        if (firstSpace == -1 || secondSpace == -1 || thirdSpace == -1 || quoteStart == -1 || quoteEnd == -1)
            return; // malformed

        int x = params.substring(0, firstSpace).toInt();
        int y = params.substring(firstSpace + 1, secondSpace).toInt();
        uint16_t color = params.substring(secondSpace + 1, thirdSpace).toInt();
        String text = params.substring(quoteStart + 1, quoteEnd);

        tft.setTextColor(color);
        tft.setCursor(x, y + TOP_BAR_HEIGHT);
        tft.print(text);
    }

    void handleDrawLine(const String &params)
    {
        // Format: DrawLine X1 Y1 X2 Y2 COLOR
        int paramsArray[5];

        int startIdx = 0;
        for (int i = 0; i < 5; i++)
        {
            int spaceIdx = params.indexOf(' ', startIdx);
            if (i < 4)
            {
                if (spaceIdx == -1)
                    return; // malformed
                paramsArray[i] = params.substring(startIdx, spaceIdx).toInt();
                startIdx = spaceIdx + 1;
            }
            else
            {
                paramsArray[i] = params.substring(startIdx).toInt();
            }
        }

        tft.drawLine(paramsArray[0], paramsArray[1] + TOP_BAR_HEIGHT,
                     paramsArray[2], paramsArray[3] + TOP_BAR_HEIGHT,
                     paramsArray[4]);
    }

    void handleDrawRect(const String &params)
    {
        // Format: DrawRect X Y W H COLOR
        int paramsArray[5];

        int startIdx = 0;
        for (int i = 0; i < 5; i++)
        {
            int spaceIdx = params.indexOf(' ', startIdx);
            if (i < 4)
            {
                if (spaceIdx == -1)
                    return;
                paramsArray[i] = params.substring(startIdx, spaceIdx).toInt();
                startIdx = spaceIdx + 1;
            }
            else
            {
                paramsArray[i] = params.substring(startIdx).toInt();
            }
        }

        tft.drawRect(paramsArray[0], paramsArray[1] + TOP_BAR_HEIGHT,
                     paramsArray[2], paramsArray[3], paramsArray[4]);
    }

    void handlePushSVG(const String &params)
    {
        // Format: PushSVG X Y W H "svg_data"
        int firstSpace = params.indexOf(' ');
        int secondSpace = params.indexOf(' ', firstSpace + 1);
        int thirdSpace = params.indexOf(' ', secondSpace + 1);
        int fourthSpace = params.indexOf(' ', thirdSpace + 1);
        int quoteStart = params.indexOf('"');
        int quoteEnd = params.lastIndexOf('"');

        if (firstSpace == -1 || secondSpace == -1 || thirdSpace == -1 || fourthSpace == -1 || quoteStart == -1 || quoteEnd == -1)
            return;

        int x = params.substring(0, firstSpace).toInt();
        int y = params.substring(firstSpace + 1, secondSpace).toInt();
        int w = params.substring(secondSpace + 1, thirdSpace).toInt();
        int h = params.substring(thirdSpace + 1, fourthSpace).toInt();
        String svgData = params.substring(quoteStart + 1, quoteEnd);

        drawSVGString(svgData, x, y + TOP_BAR_HEIGHT, w, h, TFT_WHITE);
    }

    void handleClearScreen(const String &params)
    {
        // Format: ClearScreen COLOR
        uint16_t color = params.toInt();
        tft.fillRect(0, TOP_BAR_HEIGHT, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, color);
    }

    void handleSetTextSize(const String &params)
    {
        // Format: SetTextSize SIZE
        int size = params.toInt();
        tft.setTextSize(size);
    }

    void handleSetCursor(const String &params)
    {
        // Format: SetCursor X Y
        int spaceIdx = params.indexOf(' ');
        if (spaceIdx == -1)
            return;
        int x = params.substring(0, spaceIdx).toInt();
        int y = params.substring(spaceIdx + 1).toInt();
        tft.setCursor(x, y + TOP_BAR_HEIGHT);
    }

    void handleSetTextColor(const String &params)
    {
        // Format: SetTextColor COLOR
        uint16_t color = params.toInt();
        tft.setTextColor(color);
    }

    void drawTopBar()
    {
        // Draw top bar background
        tft.fillRect(0, 0, VIEWPORT_WIDTH, TOP_BAR_HEIGHT, Style::Colors::primary);

        // Draw back button (triangle pointing left)
        tft.fillTriangle(5, 10, 15, 5, 15, 15, Style::Colors::accentText);

        // Draw forward button (triangle pointing right)
        tft.fillTriangle(25, 5, 25, 15, 35, 10, Style::Colors::accentText);

        // Draw refresh button
        tft.fillCircle(50, 10, 6, Style::Colors::accentText);

        // URL input background (clamped width)
        int urlX, urlW;
        computeUrlInputArea(urlX, urlW);
        tft.fillRect(urlX, 5, urlW, 10, Style::Colors::bg);
        tft.drawRect(urlX, 5, urlW, 10, Style::Colors::accent);

        // Draw URL text
        tft.setTextColor(Style::Colors::text);
        tft.setTextSize(1);
        tft.setCursor(urlX + 2, 7);
        tft.print(loc.domain);

        // Draw exit button
        tft.fillRect(VIEWPORT_WIDTH - 70, 5, 30, 10, Style::Colors::danger);
        tft.setTextColor(Style::Colors::accentText);
        tft.setCursor(VIEWPORT_WIDTH - 65, 7);
        tft.print("Exit");

        // Draw settings button
        tft.fillCircle(VIEWPORT_WIDTH - 30, 10, 6, Style::Colors::accent3);
    }

    void handleTouchInTopBar(int x, int y)
    {
        if (y > TOP_BAR_HEIGHT)
            return;

        // Back button (5-20px)
        if (x >= 5 && x <= 20)
        {
            // Navigate back in history (todo)
        }
        // Forward button (25-40px)
        else if (x >= 25 && x <= 40)
        {
            // Navigate forward in history (todo)
        }
        // Refresh button (45-55px)
        else if (x >= 45 && x <= 55)
        {
            // Send refresh command
            if (wsConnected)
                webSocket.sendTXT("Refresh");
        }
        // URL input area (computed)
        else
        {
            int urlX, urlW;
            computeUrlInputArea(urlX, urlW);
            if (x >= urlX && x <= (urlX + urlW))
            {
                // Activate URL input
                inputActive = true;
                currentInput = loc.domain;
                inputCursorPos = currentInput.length();
                return;
            }

            // Exit button
            if (x >= VIEWPORT_WIDTH - 70 && x <= VIEWPORT_WIDTH - 40)
            {
                Exit();
                return;
            }

            // Settings button (approx)
            if (x >= VIEWPORT_WIDTH - 35 && x <= VIEWPORT_WIDTH - 25)
            {
                // Open settings (todo)
                return;
            }
        }
    }

    void parseServerCommand(const String &message)
    {
        int spaceIdx = message.indexOf(' ');
        String command;
        String params;

        if (spaceIdx == -1)
        {
            // no params
            command = message;
            params = "";
        }
        else
        {
            command = message.substring(0, spaceIdx);
            params = message.substring(spaceIdx + 1);
        }

        if (command == "FillRect")
        {
            handleFillRect(params);
        }
        else if (command == "DrawString")
        {
            handleDrawString(params);
        }
        else if (command == "DrawLine")
        {
            handleDrawLine(params);
        }
        else if (command == "DrawRect")
        {
            handleDrawRect(params);
        }
        else if (command == "PushSVG")
        {
            handlePushSVG(params);
        }
        else if (command == "ClearScreen")
        {
            handleClearScreen(params);
        }
        else if (command == "SetTextSize")
        {
            handleSetTextSize(params);
        }
        else if (command == "SetCursor")
        {
            handleSetCursor(params);
        }
        else if (command == "SetTextColor")
        {
            handleSetTextColor(params);
        }
        else if (command == "Navigate")
        {
            loc.state = params;
            // Update display (server-driven)
        }
        else if (command == "SetSession")
        {
            ENC_FS::writeFileString(ENC_FS::storagePath("browser", loc.domain), params);
        }
        else if (command == "GetSession")
        {
            PendingResponse resp;
            resp.returnId = params;
            resp.type = "session";
            pendingResponses.push_back(resp);
        }
        else if (command == "GetState")
        {
            PendingResponse resp;
            resp.returnId = params;
            resp.type = "state";
            pendingResponses.push_back(resp);
        }
        else if (command == "GetText")
        {
            PendingResponse resp;
            resp.returnId = params;
            resp.type = "text";
            pendingResponses.push_back(resp);
        }
    }

    void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
    {
        switch (type)
        {
        case WStype_DISCONNECTED:
            Serial.printf("[WSc] Disconnected!\n");
            wsConnected = false;
            break;

        case WStype_CONNECTED:
            Serial.printf("[WSc] Connected to url: %s\n", payload);
            wsConnected = true;
            // Send handshake
            webSocket.sendTXT("MWOSP-v1 " + Location::sessionId + " " +
                              String(VIEWPORT_WIDTH) + " " + String(VIEWPORT_HEIGHT));
            break;

        case WStype_TEXT:
        {
            String message = String((char *)payload);
            Serial.printf("[WSc] Received: %s\n", message);

            // Check for handshake response
            if (message.startsWith("MWOSP-v1 OK"))
            {
                // Handshake successful -> request initial render immediately
                Serial.println("Handshake successful");
                ReRender();
            }
            else
            {
                parseServerCommand(message);
            }
        }
        break;

        default:
            break;
        }
    }

    void connectToServer()
    {
        // Parse domain:port stored in loc.domain. Accept host:port or host only.
        int colonIdx = loc.domain.indexOf(':');
        String host = loc.domain;
        int port = 80; // default to standard WS/HTTP port

        if (colonIdx != -1)
        {
            host = loc.domain.substring(0, colonIdx);
            port = loc.domain.substring(colonIdx + 1).toInt();
            if (port <= 0)
                port = 80;
        }
        else
        {
            // no explicit port provided; use 80 as standard default
            port = 80;
        }

        Serial.printf("Connecting to %s:%d\n", host.c_str(), port);

        if (port == 443)
        {
            // use secure websocket (wss)
            webSocket.beginSSL(host.c_str(), port, "/");
            // If certificate validation fails, either:
            // - provide CA cert with webSocket.setCACert(...);
            // - or if library supports it, disable validation for testing (not recommended for production).
        }
        else
        {
            // plain websocket (ws)
            webSocket.begin(host.c_str(), port, "/");
        }

        webSocket.onEvent(webSocketEvent);
        webSocket.setReconnectInterval(RECONNECT_INTERVAL);
        webSocket.enableHeartbeat(15000, 3000, 2);
    }

    void processPendingResponses()
    {
        for (size_t i = 0; i < pendingResponses.size(); i++)
        {
            String response;

            if (pendingResponses[i].type == "session")
            {
                String sessionData = "";
                if (ENC_FS::exists(ENC_FS::storagePath("browser", loc.domain)))
                {
                    sessionData = ENC_FS::readFileString(
                        ENC_FS::storagePath("browser", loc.domain));
                }
                response = "GetBackSession " + pendingResponses[i].returnId + " " + sessionData;
            }
            else if (pendingResponses[i].type == "state")
            {
                response = "GetBackState " + pendingResponses[i].returnId + " " + loc.state;
            }
            else if (pendingResponses[i].type == "text")
            {
                // Show input dialog (blocking)
                String input = readString("Server requests input:", "");
                response = "GetBackText " + pendingResponses[i].returnId + " " + input;
            }

            if (wsConnected)
            {
                webSocket.sendTXT(response);
            }

            // Remove processed response
            pendingResponses.erase(pendingResponses.begin() + i);
            i--; // Adjust index after removal
        }
    }

    void handleTouchInput()
    {
        if (!Screen::isTouched())
            return;

        Screen::TouchPos touch = Screen::getTouchPos();

        // Handle top bar touches
        if (touch.y <= TOP_BAR_HEIGHT)
        {
            handleTouchInTopBar(touch.x, touch.y);
            return;
        }

        // Send click to server
        if (touch.clicked && wsConnected)
        {
            String message = "Click " + String(touch.x) + " " + String(touch.y - TOP_BAR_HEIGHT);
            webSocket.sendTXT(message);
        }
    }

    void handleKeyboardInput()
    {
        // Check for serial input (for debugging)
        if (Serial.available())
        {
            String input = Serial.readStringUntil('\n');
            input.trim();

            if (input.length() > 0)
            {
                if (wsConnected)
                {
                    webSocket.sendTXT("Input " + input);
                }
            }
        }
    }

    void ReRender()
    {
        // Clear content area (not top bar)
        tft.fillRect(0, TOP_BAR_HEIGHT, VIEWPORT_WIDTH, VIEWPORT_HEIGHT, Style::Colors::bg);

        // Request full render from server
        if (wsConnected)
        {
            webSocket.sendTXT("NeedRender");
        }
    }

    void Update()
    {
        // Maintain WebSocket connection
        webSocket.loop();

        // Try to reconnect if disconnected
        if (!wsConnected && millis() - lastReconnectAttempt > RECONNECT_INTERVAL)
        {
            lastReconnectAttempt = millis();
            connectToServer();
        }

        // Process pending server requests
        processPendingResponses();

        // Handle user input
        handleTouchInput();
        handleKeyboardInput();

        // Handle URL input if active
        if (inputActive)
        {
            // Show keyboard and get input
            String newUrl = readString("Enter URL:", currentInput);
            if (newUrl.length() > 0)
            {
                // Parse and navigate to new URL
                Start(newUrl);
            }
            inputActive = false;
        }
    }

    void Start(const String &url)
    {
        Screen::tft.fillScreen(Style::Colors::bg);

        // Parse URL format: [scheme://]domain[:port][@state]
        int atIdx = url.indexOf('@');
        String domainPart;
        String statePart;

        if (atIdx != -1)
        {
            domainPart = url.substring(0, atIdx);
            statePart = url.substring(atIdx + 1);
        }
        else
        {
            domainPart = url;
            statePart = "startpage";
        }

        domainPart.trim();

        // Detect scheme
        bool explicitScheme = false;
        bool schemeIsSSL = false;
        String lower = domainPart;
        lower.toLowerCase();

        if (lower.startsWith("wss://") || lower.startsWith("https://"))
        {
            explicitScheme = true;
            schemeIsSSL = true;
            int idx = domainPart.indexOf("://");
            if (idx != -1)
                domainPart = domainPart.substring(idx + 3);
        }
        else if (lower.startsWith("ws://") || lower.startsWith("http://"))
        {
            explicitScheme = true;
            schemeIsSSL = false;
            int idx = domainPart.indexOf("://");
            if (idx != -1)
                domainPart = domainPart.substring(idx + 3);
        }

        domainPart.trim();

        // If scheme was explicit and no port provided, append standard port
        if (explicitScheme && domainPart.indexOf(':') == -1)
        {
            if (schemeIsSSL)
                domainPart += ":443";
            else
                domainPart += ":80";
        }

        loc.domain = domainPart;
        loc.state = statePart;

        // If caller didn't specify a port and no explicit scheme, we leave domain without port.
        // connectToServer() will default to port 80 in that case.

        Serial.println("Starting browser with:");
        Serial.println("  Domain: " + loc.domain);
        Serial.println("  State: " + loc.state);

        // Load session data if exists
        if (ENC_FS::exists(ENC_FS::storagePath("browser", loc.domain)))
        {
            loc.session = ENC_FS::readFileString(
                ENC_FS::storagePath("browser", loc.domain));
        }

        // Draw top bar
        drawTopBar();

        // Connect to server
        connectToServer();

        isRunning = true;
    }

    void Start()
    {
        // Default start page (no forced custom port)
        Start("mw-search-server-onrender-app.onrender.com@startpage");
    }

    void Exit()
    {
        isRunning = false;
    }

    void OnExit()
    {
        Screen::tft.fillScreen(Style::Colors::bg);
        // Disconnect WebSocket
        webSocket.disconnect();
        wsConnected = false;

        // Save any pending data
        if (loc.session.length() > 0)
        {
            ENC_FS::writeFileString(
                ENC_FS::storagePath("browser", loc.domain),
                loc.session);
        }

        Serial.println("Browser closed");
        Screen::tft.fillScreen(Style::Colors::bg);
    }
} // namespace Browser

void openBrowser()
{
    Browser::isRunning = true;
    Browser::Start();

    while (Browser::isRunning)
    {
        Browser::Update();
        vTaskDelay(10);
    }

    Browser::OnExit();
}

// Optional: Function to open browser with specific URL
void openBrowser(const String &url)
{
    Browser::isRunning = true;
    Browser::Start(url);

    while (Browser::isRunning)
    {
        Browser::Update();
        vTaskDelay(10);
    }

    Browser::OnExit();
}
