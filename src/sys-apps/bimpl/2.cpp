#pragma once

#include <Arduino.h>
#include <WebSocketsClient.h>
#include <WiFi.h>

// Include your provided headers
#include "../screen/index.hpp"
#include "../io/read-string.hpp"
#include "../styles/global.hpp"
#include "../fs/enc-fs.hpp"
#include "../utils/vec.hpp" // Assuming Vec is here based on Screen::TouchPos

// Define Protocol Constants
#define TOP_BAR_HEIGHT 25
#define DEFAULT_SERVER "mw-search-server.onrender.app"
#define DEFAULT_PORT 6767
#define PROTOCOL_VER "MWOSP-v1"

namespace Browser
{
    // --- State Definitions ---
    struct Location
    {
        String domain;           // "host:port"
        String state;            // Current app state (e.g. "home", "search|query")
        String session;          // Session token/data
        static String sessionId; // Global random ID for this device/boot
    };

    // Define static members
    String Location::sessionId = "";
    static Location loc;
    static bool isRunning = false;
    static WebSocketsClient webSocket;
    static bool isConnected = false;
    static bool uiDirty = true; // Request to redraw top bar

    // --- Helper: Parse Host/Port from "domain:port" ---
    struct HostPort
    {
        String host;
        int port;
    };
    HostPort parseDomain(const String &dom)
    {
        int colon = dom.indexOf(':');
        if (colon == -1)
            return {dom, 80};
        return {dom.substring(0, colon), dom.substring(colon + 1).toInt()};
    }

    // --- Storage Helpers ---
    void loadSession()
    {
        // Reads session from encrypted FS: /browser/<domain>/session
        ENC_FS::Buffer data = ENC_FS::Storage::get("browser", loc.domain);
        if (!data.empty())
        {
            // Convert Buffer to String
            char *ptr = (char *)data.data();
            loc.session = String(ptr).substring(0, data.size());
        }
        else
        {
            loc.session = "";
        }
    }

    void saveSession()
    {
        if (loc.session.length() == 0)
            return;
        ENC_FS::Buffer data(loc.session.begin(), loc.session.end());
        ENC_FS::Storage::set("browser", loc.domain, data);
    }

    // --- UI: Top Bar (Chrome) ---
    void drawChrome()
    {
        // Background
        Screen::tft.fillRect(0, 0, Screen::tft.width(), TOP_BAR_HEIGHT, Style::Colors::bg);
        Screen::tft.drawLine(0, TOP_BAR_HEIGHT, Screen::tft.width(), TOP_BAR_HEIGHT, Style::Colors::accent);

        // URL Box
        Screen::tft.setTextColor(Style::Colors::text, Style::Colors::bg);
        Screen::tft.setTextDatum(ML_DATUM);
        String displayUrl = loc.domain + "@" + loc.state;
        if (displayUrl.length() > 25)
            displayUrl = displayUrl.substring(0, 22) + "...";
        Screen::tft.drawString(displayUrl, 5, TOP_BAR_HEIGHT / 2);

        // Exit Button (Right side)
        int exitX = Screen::tft.width() - 25;
        Screen::tft.fillRect(exitX, 2, 22, 21, Style::Colors::danger);
        Screen::tft.setTextColor(Style::Colors::text);
        Screen::tft.setTextDatum(MC_DATUM);
        Screen::tft.drawString("X", exitX + 11, TOP_BAR_HEIGHT / 2 + 1);

        // Status Dot (Green = Connected, Red = Disconnected)
        uint16_t statusColor = isConnected ? 0x07E0 : 0xF800; // Green : Red
        Screen::tft.fillCircle(Screen::tft.width() - 35, TOP_BAR_HEIGHT / 2, 3, statusColor);
    }

    // --- Protocol Command Parsers ---

    // Send a safe string to server
    void send(String msg)
    {
        if (isConnected)
            webSocket.sendTXT(msg);
    }

    // Handlers for specific server commands
    void handleFillRect(String args)
    {
        // Format: X Y W H COLOR(16bit)
        int params[5];
        int count = 0;
        int lastIdx = 0;
        for (int i = 0; i < 5; i++)
        {
            int nextIdx = args.indexOf(' ', lastIdx);
            if (nextIdx == -1)
                nextIdx = args.length();
            params[i] = args.substring(lastIdx, nextIdx).toInt();
            lastIdx = nextIdx + 1;
            count++;
            if (lastIdx >= args.length() && i < 4)
                break;
        }

        if (count == 5)
        {
            // Apply Offset for content area
            Screen::tft.fillRect(params[0], params[1] + TOP_BAR_HEIGHT, params[2], params[3], (uint16_t)params[4]);
        }
    }

    void handlePrintText(String args)
    {
        // Format: X Y SIZE COLOR TEXT...
        // We need to parse X Y SIZE COLOR manually, then take the rest as string
        int x = args.substring(0, args.indexOf(' ')).toInt();
        args = args.substring(args.indexOf(' ') + 1);
        int y = args.substring(0, args.indexOf(' ')).toInt();
        args = args.substring(args.indexOf(' ') + 1);
        int size = args.substring(0, args.indexOf(' ')).toInt();
        args = args.substring(args.indexOf(' ') + 1);
        int color = args.substring(0, args.indexOf(' ')).toInt();
        String text = args.substring(args.indexOf(' ') + 1);

        Screen::tft.setTextColor((uint16_t)color);
        Screen::tft.setTextSize(size);
        Screen::tft.setTextDatum(TL_DATUM); // Top Left
        Screen::tft.drawString(text, x, y + TOP_BAR_HEIGHT);
    }

    void handlePushSvg(String args)
    {
        // Format: X Y W H COLOR SVG_STRING...
        // Parsing similar to text
        int split[5];
        int lastIdx = 0;
        for (int i = 0; i < 5; i++)
        {
            int nextIdx = args.indexOf(' ', lastIdx);
            split[i] = args.substring(lastIdx, nextIdx).toInt();
            lastIdx = nextIdx + 1;
        }
        String svgContent = args.substring(lastIdx);

        // Use the provided drawSVGString function
        drawSVGString(svgContent, split[0], split[1] + TOP_BAR_HEIGHT, split[2], split[3], (uint16_t)split[4]);
    }

    void handleRequests(String type, String returnId)
    {
        if (type == "GetSession")
        {
            send("GetBackSession " + returnId + " " + loc.session);
        }
        else if (type == "GetState")
        {
            send("GetBackState " + returnId + " " + loc.state);
        }
        else if (type == "GetText")
        {
            // Server wants user input via keyboard
            String input = readString("Server Request:", "");
            // Restore screen after full-screen keyboard
            uiDirty = true;
            send("GetBackText " + returnId + " " + input);
        }
    }

    // --- WebSocket Event Handler ---
    void webSocketEvent(WStype_t type, uint8_t *payload, size_t length)
    {
        switch (type)
        {
        case WStype_DISCONNECTED:
            isConnected = false;
            uiDirty = true;
            break;
        case WStype_CONNECTED:
        {
            isConnected = true;
            uiDirty = true;

            // 1. Send Handshake
            // Format: MWOSP-v1 sessionId VIEWPORTX VIEWPORTY
            String handshake = String(PROTOCOL_VER) + " " +
                               loc.sessionId + " " +
                               String(Screen::tft.width()) + " " +
                               String(Screen::tft.height() - TOP_BAR_HEIGHT);
            webSocket.sendTXT(handshake);

            // 2. Send current state if we have one
            if (loc.state.length() > 0)
            {
                webSocket.sendTXT("Navigate " + loc.state);
            }
            break;
        }
        case WStype_TEXT:
        {
            String cmdLine = String((char *)payload);
            int spaceIdx = cmdLine.indexOf(' ');
            String cmd = (spaceIdx == -1) ? cmdLine : cmdLine.substring(0, spaceIdx);
            String args = (spaceIdx == -1) ? "" : cmdLine.substring(spaceIdx + 1);

            // --- Rendering Commands ---
            if (cmd == "FillReact" || cmd == "FillRect")
            { // Handle both just in case
                handleFillRect(args);
            }
            else if (cmd == "PrintText")
            {
                handlePrintText(args);
            }
            else if (cmd == "PushSvg")
            {
                handlePushSvg(args);
            }
            else if (cmd == "PushImage")
            {
                // Placeholder: The prompt didn't provide a generic image function aside from SD
                // If server sends raw RGB565, we could implement pushImage here.
                // Ignoring to prevent crash if not implemented.
            }
            else if (cmd == "PrintPx")
            {
                // Format: X Y COLOR
                int x = args.substring(0, args.indexOf(' ')).toInt();
                args = args.substring(args.indexOf(' ') + 1);
                int y = args.substring(0, args.indexOf(' ')).toInt();
                int color = args.substring(args.indexOf(' ') + 1).toInt();
                Screen::tft.drawPixel(x, y + TOP_BAR_HEIGHT, (uint16_t)color);
            }
            // --- Logic Commands ---
            else if (cmd == "Navigate")
            {
                loc.state = args;
                uiDirty = true; // Update URL bar
            }
            else if (cmd == "SetSession")
            {
                loc.session = args;
                saveSession();
            }
            else if (cmd == "GetSession" || cmd == "GetState" || cmd == "GetText")
            {
                handleRequests(cmd, args);
            }
            else if (cmd == "MWOSP-v1" && args == "OK")
            {
                // Server Ack, nothing to do visually
            }
            break;
        }
        case WStype_BIN:
        case WStype_ERROR:
        case WStype_FRAGMENT_TEXT_START:
        case WStype_FRAGMENT_BIN_START:
        case WStype_FRAGMENT:
        case WStype_FRAGMENT_FIN:
            break;
        }
    }

    // --- Lifecycle Methods ---

    void Start()
    {
        // 1. Generate Session ID if new
        if (loc.sessionId == "")
        {
            loc.sessionId = String(random(0xFFFFFFFF), HEX);
        }

        // 2. Default Domain if empty
        if (loc.domain == "")
        {
            loc.domain = String(DEFAULT_SERVER) + ":" + String(DEFAULT_PORT);
            loc.state = "startpage";
        }

        // 3. Load previous session data for this domain
        loadSession();

        // 4. Initialize WebSocket
        HostPort hp = parseDomain(loc.domain);

        // Secure or insecure? Assuming insecure for generic "ws://" unless port 443
        // For standard internal/render servers, usually standard WS or WSS.
        // WebSocketsClient handles WSS if we use beginSSL.
        // We will stick to standard begin() as usually ESP32 overhead for SSL + Rendering is high.
        webSocket.begin(hp.host, hp.port, "/");
        webSocket.onEvent(webSocketEvent);
        webSocket.setReconnectInterval(3000); // Auto reconnect every 3s

        // 5. Initial Draw
        Screen::tft.fillScreen(Style::Colors::bg);
        drawChrome();
    }

    void ReRender()
    {
        drawChrome();
        // We cannot locally re-render the content area as it is server-driven.
        // We could request a refresh from server?
        if (isConnected)
            send("Navigate " + loc.state);
    }

    void Stop()
    {
        webSocket.disconnect();
        isConnected = false;
    }

    void OnExit()
    {
        Stop();
        Screen::tft.fillScreen(TFT_BLACK);
    }

    void handleInput()
    {
        if (Screen::isTouched())
        {
            Screen::TouchPos pos = Screen::getTouchPos();
            if (pos.clicked)
            { // Only on initial press or release, depending on logic

                // Top Bar Interaction
                if (pos.y < TOP_BAR_HEIGHT)
                {
                    // Clicked on Exit
                    if (pos.x > Screen::tft.width() - 30)
                    {
                        isRunning = false; // Break loop
                    }
                    // Clicked on URL Bar
                    else
                    {
                        String newUrl = readString("Go to:", loc.domain);
                        if (newUrl != "" && newUrl != loc.domain)
                        {
                            Stop(); // Disconnect old

                            // Handle input parsing "domain:port@state"
                            int atIdx = newUrl.indexOf('@');
                            if (atIdx != -1)
                            {
                                loc.domain = newUrl.substring(0, atIdx);
                                loc.state = newUrl.substring(atIdx + 1);
                            }
                            else
                            {
                                loc.domain = newUrl;
                                loc.state = ""; // Reset state on domain change
                            }

                            Start(); // Reconnect
                        }
                        // Redraw UI after keyboard closed
                        uiDirty = true;
                    }
                }
                // Content Area Interaction
                else
                {
                    if (isConnected)
                    {
                        // Send click relative to viewport
                        int relY = pos.y - TOP_BAR_HEIGHT;
                        if (relY >= 0)
                        {
                            send("Click " + String(pos.x) + " " + String(relY));
                        }
                    }
                }
            }
        }
    }

    void Update()
    {
        webSocket.loop();
        handleInput();

        if (uiDirty)
        {
            drawChrome();
            uiDirty = false;
        }
    }
}

// --- Main Entry Point ---

static inline void openBrowser()
{
    // Ensure WiFi is connected before starting
    if (WiFi.status() != WL_CONNECTED)
    {
        Screen::tft.fillScreen(TFT_RED);
        Screen::tft.setTextColor(TFT_WHITE);
        Screen::tft.drawString("No WiFi!", 10, 10);
        delay(2000);
        return;
    }

    Browser::isRunning = true;
    Browser::Start();

    while (Browser::isRunning)
    {
        Browser::Update();
        // Small delay to prevent WDT trigger if loop is tight,
        // though WebSocketsClient calls yield() internally.
        vTaskDelay(5 / portTICK_PERIOD_MS);
    }

    Browser::OnExit();
}