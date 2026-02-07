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
        tft.setCursor(10, 10);
        tft.setTextColor(TFT_WHITE);
        tft.println("Connecting to MWOSP...");

        // Start WebSocket on port 443
        webSocket.beginSSL(loc.domain.c_str(), loc.port, "/");

        // Use insecure mode to skip certificate fingerprint check for easy deployment
        // (Render uses Let's Encrypt certificates)
        webSocket.setReconnectInterval(5000);

        webSocket.onEvent([](WStype_t type, uint8_t *payload, size_t length)
                          {
            String msg = (payload != nullptr) ? (char *)payload : "";
            
            switch (type)
            {
            case WStype_CONNECTED:
                Serial.println("[Browser] Connected to Server");
                webSocket.sendTXT("MWOSP-v1 " + Location::sessionId);
                break;
            case WStype_TEXT:
                handleCommand(msg);
                break;
            case WStype_DISCONNECTED:
                Serial.println("[Browser] Disconnected");
                break;
            } });
    }

    void handleCommand(String payload)
    {
        if (payload.startsWith("FillRect"))
        {
            // Protocol: FillRect X Y W H COLOR16
            int x, y, w, h;
            uint16_t c;
            if (sscanf(payload.c_str(), "FillRect %d %d %d %d %hu", &x, &y, &w, &h, &c) == 5)
            {
                tft.fillRect(x, y, w, h, c);
            }
        }
        else if (payload.startsWith("Navigate"))
        {
            loc.state = payload.substring(9);
        }
        else if (payload.startsWith("GetText"))
        {
            String rid = payload.substring(8);
            // Example hook for your read-string.hpp:
            // String input = readString("Input Required", "");
            // webSocket.sendTXT("GetBackText " + rid + " " + input);
        }
    }

    void Update()
    {
        webSocket.loop();

        if (Screen::isTouched())
        {
            Screen::TouchPos pos = Screen::getTouchPos();
            if (pos.clicked)
            {
                webSocket.sendTXT("Click " + String(pos.x) + " " + String(pos.y));
            }
        }
    }

    void Exit() { isRunning = false; }
    void OnExit() { webSocket.disconnect(); }
}