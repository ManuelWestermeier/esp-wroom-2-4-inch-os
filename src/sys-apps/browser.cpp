#include "browser.hpp"
#include <WiFiClientSecure.h>

namespace Browser
{
    Location loc;
    bool isRunning = false;
    WebSocketsClient webSocket;
    String Location::sessionId = String(random(1000, 9999));

    void Start()
    {
        tft.fillScreen(TFT_BLACK);
        tft.setCursor(10, 10);
        tft.setTextColor(TFT_WHITE);
        tft.println("Connecting to MWOSP...");

        // Setup WebSocket
        webSocket.beginSSL(loc.domain.c_str(), loc.port, "/");
        webSocket.onEvent([](WStype_t type, uint8_t *payload, size_t length)
                          {
            String msg = (char*)payload;
            switch(type) {
                case WStype_CONNECTED:
                    webSocket.sendTXT("MWOSP-v1 " + Location::sessionId);
                    break;
                case WStype_TEXT:
                    handleCommand(msg);
                    break;
            } });
    }

    void handleCommand(String payload)
    {
        if (payload.startsWith("FillRect"))
        {
            // Format: FillRect X Y W H COLOR16
            int x, y, w, h;
            uint16_t c;
            sscanf(payload.c_str(), "FillRect %d %d %d %d %hu", &x, &y, &w, &h, &c);
            tft.fillRect(x, y, w, h, c);
        }
        else if (payload.startsWith("Navigate"))
        {
            loc.state = payload.substring(9);
        }
        else if (payload.startsWith("GetText"))
        {
            String rid = payload.substring(8);
            // This would trigger your readString keyboard UI
            // String input = readString("Server Query", "");
            webSocket.sendTXT("GetBackText " + rid + " UserInput");
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