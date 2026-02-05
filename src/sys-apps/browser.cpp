#include "browser.hpp"
#include <WebSocketsClient.h>
#include <ArduinoJson.h>

namespace Browser
{
    String Location::sessionId = String(random(0xFFFFFFFF), HEX);
    Location loc;
    bool isRunning = false;
    WebSocketsClient webSocket;
    bool wsConnected = false;
    unsigned long lastReconnectAttempt = 0;
    const unsigned long RECONNECT_INTERVAL = 5000;

    // Screen dimensions
    const int VIEWPORT_WIDTH = TFT_WIDTH;
    const int VIEWPORT_HEIGHT = TFT_HEIGHT - 20; // Reserve 20px for top bar

    // Rendering state
    struct PendingResponse
    {
        String returnId;
        String type; // "session", "state", "text"
    };
    std::vector<PendingResponse> pendingResponses;

    // UI Elements
    const int TOP_BAR_HEIGHT = 20;
    String currentInput = "";
    bool inputActive = false;
    int inputCursorPos = 0;

    // Server command handlers
    void handleFillRect(const String &params)
    {
        // Format: FillRect X Y W H COLOR
        int paramsArray[5];
        int color;

        int startIdx = 0;
        for (int i = 0; i < 5; i++)
        {
            int spaceIdx = params.indexOf(' ', startIdx);
            if (i < 4)
            {
                paramsArray[i] = params.substring(startIdx, spaceIdx).toInt();
                startIdx = spaceIdx + 1;
            }
            else
            {
                color = params.substring(startIdx).toInt();
            }
        }

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

        // Draw back button
        tft.fillTriangle(5, 10, 15, 5, 15, 15, Style::Colors::accentText);

        // Draw forward button
        tft.fillTriangle(25, 5, 25, 15, 35, 10, Style::Colors::accentText);

        // Draw refresh button
        tft.fillCircle(50, 10, 6, Style::Colors::accentText);

        // Draw URL input background
        tft.fillRect(65, 5, VIEWPORT_WIDTH - 140, 10, Style::Colors::bg);
        tft.drawRect(65, 5, VIEWPORT_WIDTH - 140, 10, Style::Colors::accent);

        // Draw URL text
        tft.setTextColor(Style::Colors::text);
        tft.setTextSize(1);
        tft.setCursor(67, 7);
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
            // Navigate back in history
        }
        // Forward button (25-40px)
        else if (x >= 25 && x <= 40)
        {
            // Navigate forward in history
        }
        // Refresh button (45-55px)
        else if (x >= 45 && x <= 55)
        {
            // Send refresh command
            webSocket.sendTXT("Refresh");
        }
        // URL input area (65px to width-140px)
        else if (x >= 65 && x <= VIEWPORT_WIDTH - 140)
        {
            // Activate URL input
            inputActive = true;
            currentInput = loc.domain;
            inputCursorPos = currentInput.length();
        }
        // Exit button
        else if (x >= VIEWPORT_WIDTH - 70 && x <= VIEWPORT_WIDTH - 40)
        {
            Exit();
        }
        // Settings button
        else if (x >= VIEWPORT_WIDTH - 35 && x <= VIEWPORT_WIDTH - 25)
        {
            // Open settings
        }
    }

    void parseServerCommand(const String &message)
    {
        int spaceIdx = message.indexOf(' ');
        String command = message.substring(0, spaceIdx);
        String params = message.substring(spaceIdx + 1);

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
            // Update display
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
                // Handshake successful
                Serial.println("Handshake successful");
            }
            else
            {
                parseServerCommand(message);
            }
        }
        break;

        case WStype_ERROR:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
        case WStype_BIN:
        case WStype_PING:
        case WStype_PONG:
            break;
        }
    }

    void connectToServer()
    {
        // Parse domain:port
        int colonIdx = loc.domain.indexOf(':');
        String host = loc.domain;
        int port = 6767; // Default port

        if (colonIdx != -1)
        {
            host = loc.domain.substring(0, colonIdx);
            port = loc.domain.substring(colonIdx + 1).toInt();
        }

        Serial.printf("Connecting to %s:%d\n", host.c_str(), port);

        // Begin connection
        webSocket.begin(host.c_str(), port, "/");
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
                String sessionData = ENC_FS::readFileString(
                    ENC_FS::storagePath("browser", loc.domain));
                response = "GetBackSession " + pendingResponses[i].returnId + " " + sessionData;
            }
            else if (pendingResponses[i].type == "state")
            {
                response = "GetBackState " + pendingResponses[i].returnId + " " + loc.state;
            }
            else if (pendingResponses[i].type == "text")
            {
                // Show input dialog
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
        // Parse URL format: domain:port@state
        int atIdx = url.indexOf('@');
        int colonIdx = url.indexOf(':');

        if (atIdx != -1)
        {
            loc.domain = url.substring(0, atIdx);
            loc.state = url.substring(atIdx + 1);
        }
        else
        {
            loc.domain = url;
            loc.state = "startpage";
        }

        // Ensure domain has port
        if (loc.domain.indexOf(':') == -1)
        {
            loc.domain += ":6767";
        }

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
        // Default start page
        Start("mw-search-server.onrender.com:6767@startpage");
    }

    void Exit()
    {
        isRunning = false;
    }

    void OnExit()
    {
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
    }
}

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