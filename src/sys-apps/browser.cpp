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
        if (payload.startsWith("FillRect"))
        {
            int x, y, w, h;
            uint16_t c;
            if (sscanf(payload.c_str(), "FillRect %d %d %d %d %hu", &x, &y, &w, &h, &c) == 5)
            {
                tft.fillRect(x, y, w, h, c);
            }
        }
        else if (payload.startsWith("DrawCircle"))
        {
            int x, y, r;
            uint16_t c;
            if (sscanf(payload.c_str(), "DrawCircle %d %d %d %hu", &x, &y, &r, &c) == 4)
            {
                tft.drawCircle(x, y, r, c);
            }
        }
        else if (payload.startsWith("DrawText"))
        {
            int x, y, size;
            uint16_t c;
            char buf[256];
            if (sscanf(payload.c_str(), "DrawText %d %d %d %hu %[^\n]", &x, &y, &size, &c, buf) >= 5)
            {
                drawText(x, y, String(buf), c, size);
            }
        }
        else if (payload.startsWith("DrawSVG"))
        {
            int x, y, w, h;
            uint16_t c;
            int idx = payload.indexOf(' ', 7);
            String svg = payload.substring(idx + 1);
            drawSVG(svg, x, y, w, h, c);
        }
        else if (payload.startsWith("PromptText"))
        {
            String rid = payload.substring(11);
            String input = promptText("Enter text:");
            webSocket.sendTXT("GetBackText " + rid + " " + input);
        }
        else if (payload.startsWith("Navigate"))
        {
            loc.state = payload.substring(9);
            ReRender();
        }
        else if (payload.startsWith("Exit"))
        {
            Exit();
        }
        else if (payload.startsWith("ClearSettings"))
        {
            clearSettings();
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
        tft.fillRect(0, 0, 320, 20, 31); // top bar
        tft.setCursor(5, 5);
        tft.setTextColor(TFT_WHITE);
        tft.println(loc.state);
        // TODO: add pages, buttons, input fields
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
        {
            drawSVGString(svgStr, x, y, w, h, color);
        }
    }

    String promptText(const String &question, const String &defaultValue)
    {
        return readString(question, defaultValue);
    }

    void clearSettings()
    {
        loc.session = "";
        loc.state = "startpage";
        ENC_FS::Storage::del("browser", "settings");
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

    void storeData(const String &key, const ENC_FS::Buffer &data)
    {
        ENC_FS::Storage::set("browser", key, data);
    }

    ENC_FS::Buffer loadData(const String &key)
    {
        return ENC_FS::Storage::get("browser", key);
    }
}
