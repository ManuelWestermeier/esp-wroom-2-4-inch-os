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

        // WebSocket SSL
        webSocket.beginSSL(loc.domain.c_str(), loc.port, "/");
        webSocket.setReconnectInterval(5000);

        webSocket.onEvent([](WStype_t type, uint8_t *payload, size_t length)
                          {
            String msg = (payload != nullptr) ? (char *)payload : "";

            switch (type)
            {
            case WStype_CONNECTED:
                Serial.println("[Browser] Connected to Server");
                webSocket.sendTXT("MWOSP-v1 " + Location::sessionId + " 320 480"); // viewport
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
        if (payload.startsWith("FillRect"))
        {
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
            ReRender();
        }
        else if (payload.startsWith("GetText"))
        {
            String rid = payload.substring(8);
            String input = readString("Input Required", "");
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

        if (Screen::isTouched())
        {
            Screen::TouchPos pos = Screen::getTouchPos();
            if (pos.clicked)
            {
                webSocket.sendTXT("Click " + String(pos.x) + " " + String(pos.y));
            }
        }
    }

    void ReRender()
    {
        tft.fillScreen(TFT_BLACK);
        // Top bar
        tft.fillRect(0, 0, 320, 20, 31);
        // Render state as text
        tft.setCursor(5, 5);
        tft.setTextColor(TFT_WHITE);
        tft.println(loc.state);
        // TODO: Add input field, buttons, history, etc.
    }

    void Exit()
    {
        isRunning = false;
    }

    void OnExit()
    {
        webSocket.disconnect();
    }
}
